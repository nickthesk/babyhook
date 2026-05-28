/*
/^-----^\   data: 2026-05-06
V  o o  V  file: src/core/player_manager.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "core/player_manager.hpp"

#include "core/ipc/ipc_client.hpp"
#include "core/logger.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/netvars.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cathook::core::players
{
namespace
{

struct stored_player
{
  player_state state = player_state::default_state;
  std::string name{};
};

std::mutex player_mutex{};
std::unordered_map<std::uint32_t, stored_player> persistent_players{};
std::unordered_map<std::uint32_t, stored_player> runtime_players{};

[[nodiscard]] std::filesystem::path player_list_path()
{
  return config_directory() / "players.cat";
}

[[nodiscard]] std::string trim(std::string value)
{
  const auto is_space = [](const unsigned char character)
  {
    return std::isspace(character) != 0;
  };

  const auto start = std::find_if_not(value.begin(), value.end(), is_space);
  const auto finish = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
  if (start >= finish)
  {
    return {};
  }

  return {start, finish};
}

[[nodiscard]] std::string sanitize_name(std::string_view name)
{
  std::string sanitized{name};
  sanitized.erase(std::remove_if(sanitized.begin(), sanitized.end(), [](const char character)
  {
    return character == '\n' || character == '\r' || character == '\t';
  }), sanitized.end());
  return sanitized;
}

[[nodiscard]] std::uint32_t read_account_id(std::string_view value)
{
  std::uint32_t account_id = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), account_id);
  return result.ec == std::errc{} && result.ptr == value.data() + value.size() ? account_id : 0;
}

[[nodiscard]] bool should_persist(player_state state)
{
  return state != player_state::default_state && state != player_state::ipc && state != player_state::textmode;
}

[[nodiscard]] bool state_is_friendly(player_state state)
{
  return state == player_state::friend_state ||
         state == player_state::party ||
         state == player_state::ipc ||
         state == player_state::textmode;
}

[[nodiscard]] bool state_is_ignored(player_state state)
{
  return state == player_state::ignored;
}

[[nodiscard]] bool state_is_prioritized(player_state state)
{
  return state == player_state::cheater;
}

void set_runtime_state(std::uint32_t account_id, player_state state, std::string_view name)
{
  if (account_id == 0)
  {
    return;
  }

  runtime_players[account_id] = stored_player{state, sanitize_name(name)};
}

[[nodiscard]] Entity* get_player_resource_entity()
{
  if (entity_list == nullptr)
  {
    return nullptr;
  }

  const int max_entities = entity_list->get_max_entities();
  for (int index = 1; index <= max_entities; ++index)
  {
    auto* entity = entity_list->entity_from_index(index);
    if (entity != nullptr && entity->get_class_id() == class_id::PLAYER_RESOURCE)
    {
      return entity;
    }
  }

  return nullptr;
}

template <typename value_type>
[[nodiscard]] value_type read_player_resource_value(Entity* player_resource, int array_offset, int player_index)
{
  if (player_resource == nullptr || player_index <= 0)
  {
    return {};
  }

  const auto base = reinterpret_cast<std::uintptr_t>(player_resource);
  const auto entry_offset = static_cast<std::uintptr_t>(array_offset) + (static_cast<std::uintptr_t>(player_index) * sizeof(value_type));
  return *reinterpret_cast<value_type*>(base + entry_offset);
}

} // namespace

void initialize()
{
  load();
}

void shutdown()
{
  std::lock_guard lock{player_mutex};
  runtime_players.clear();
  persistent_players.clear();
}

void tick()
{
  if (engine == nullptr ||
      entity_list == nullptr ||
      global_vars == nullptr ||
      !engine->is_connected() ||
      !engine->is_in_game() ||
      engine->is_drawing_loading_image())
  {
    return;
  }

  auto* player_resource = get_player_resource_entity();
  if (player_resource == nullptr)
  {
    return;
  }

  static const int connected_offset = tf2_netvars::find_offset("DT_TFPlayerResource", { "baseclass", "m_bConnected" });
  if (connected_offset <= 0)
  {
    return;
  }

  std::lock_guard lock{player_mutex};
  runtime_players.clear();

  const int max_clients = global_vars->max_clients;
  for (int index = 1; index <= max_clients; ++index)
  {
    const bool is_connected = read_player_resource_value<bool>(player_resource, connected_offset, index);
    if (!is_connected)
    {
      continue;
    }

    player_info info{};
    if (!engine->get_player_info(index, &info) || info.fakeplayer || info.friends_id == 0)
    {
      continue;
    }

    const auto account_id = static_cast<std::uint32_t>(info.friends_id);
    if (cat_ipc::client::is_local_ipc_friend(account_id))
    {
      set_runtime_state(account_id, player_state::ipc, info.name);
    }
  }
}

bool load()
{
  std::lock_guard lock{player_mutex};
  persistent_players.clear();

  std::ifstream input{player_list_path()};
  if (!input.is_open())
  {
    return false;
  }

  std::string line{};
  while (std::getline(input, line))
  {
    line = trim(std::move(line));
    if (line.empty() || line.starts_with('#'))
    {
      continue;
    }

    const auto first_tab = line.find('\t');
    if (first_tab == std::string::npos)
    {
      continue;
    }

    const auto second_tab = line.find('\t', first_tab + 1);
    const auto account_id = read_account_id(std::string_view{line}.substr(0, first_tab));
    const auto state_text = second_tab == std::string::npos
      ? std::string_view{line}.substr(first_tab + 1)
      : std::string_view{line}.substr(first_tab + 1, second_tab - first_tab - 1);
    const auto parsed_state = parse_state(state_text);
    if (account_id == 0 || !parsed_state || !should_persist(*parsed_state))
    {
      continue;
    }

    const auto name = second_tab == std::string::npos ? std::string{} : sanitize_name(std::string_view{line}.substr(second_tab + 1));
    persistent_players[account_id] = stored_player{*parsed_state, name};
  }

  return true;
}

bool save()
{
  std::lock_guard lock{player_mutex};
  std::error_code error{};
  std::filesystem::create_directories(config_directory(), error);

  std::ofstream output{player_list_path(), std::ios::trunc};
  if (!output.is_open())
  {
    return false;
  }

  std::vector<std::pair<std::uint32_t, stored_player>> ordered_entries{};
  ordered_entries.reserve(persistent_players.size());
  for (const auto& entry : persistent_players)
  {
    if (should_persist(entry.second.state))
    {
      ordered_entries.emplace_back(entry.first, entry.second);
    }
  }

  std::ranges::sort(ordered_entries, [](const auto& left, const auto& right)
  {
    return left.first < right.first;
  });

  output << "# account_id\tstate\tname\n";
  for (const auto& [account_id, player] : ordered_entries)
  {
    output << account_id << '\t' << state_name(player.state) << '\t' << player.name << '\n';
  }

  return output.good();
}

bool set_state(std::uint32_t account_id, player_state state, std::string_view name, bool save_changes)
{
  if (account_id == 0)
  {
    return false;
  }

  {
    std::lock_guard lock{player_mutex};
    if (!should_persist(state))
    {
      persistent_players.erase(account_id);
    }
    else
    {
      persistent_players[account_id] = stored_player{state, sanitize_name(name)};
    }
  }

  return !save_changes || save();
}

bool clear_state(std::uint32_t account_id, bool save_changes)
{
  if (account_id == 0)
  {
    return false;
  }

  {
    std::lock_guard lock{player_mutex};
    persistent_players.erase(account_id);
  }

  return !save_changes || save();
}

player_state state_for(std::uint32_t account_id)
{
  if (account_id == 0)
  {
    return player_state::default_state;
  }

  std::lock_guard lock{player_mutex};
  if (const auto found = persistent_players.find(account_id); found != persistent_players.end())
  {
    return found->second.state;
  }

  if (const auto found = runtime_players.find(account_id); found != runtime_players.end())
  {
    return found->second.state;
  }

  return player_state::default_state;
}

bool is_friendly(std::uint32_t account_id)
{
  return state_is_friendly(state_for(account_id));
}

bool is_ignored(std::uint32_t account_id)
{
  return state_is_ignored(state_for(account_id));
}

bool is_prioritized(std::uint32_t account_id)
{
  return state_is_prioritized(state_for(account_id));
}

const char* state_name(player_state state)
{
  switch (state)
  {
    case player_state::friend_state:
      return "friend";
    case player_state::ignored:
      return "ignored";
    case player_state::cheater:
      return "cheater";
    case player_state::ipc:
      return "ipc";
    case player_state::textmode:
      return "textmode";
    case player_state::party:
      return "party";
    case player_state::default_state:
    default:
      return "default";
  }
}

std::optional<player_state> parse_state(std::string_view state_name)
{
  std::string normalized{state_name};
  normalized.erase(std::remove_if(normalized.begin(), normalized.end(), [](const unsigned char character)
  {
    return std::isspace(character) != 0 || character == '_' || character == '-';
  }), normalized.end());
  std::ranges::transform(normalized, normalized.begin(), [](const unsigned char character)
  {
    return static_cast<char>(std::tolower(character));
  });

  if (normalized == "default")
  {
    return player_state::default_state;
  }
  if (normalized == "friend")
  {
    return player_state::friend_state;
  }
  if (normalized == "ignore" || normalized == "ignored")
  {
    return player_state::ignored;
  }
  if (normalized == "cheater" || normalized == "rage")
  {
    return player_state::cheater;
  }
  if (normalized == "ipc")
  {
    return player_state::ipc;
  }
  if (normalized == "textmode")
  {
    return player_state::textmode;
  }
  if (normalized == "party")
  {
    return player_state::party;
  }

  return std::nullopt;
}

std::vector<player_entry> entries(bool include_runtime)
{
  std::lock_guard lock{player_mutex};
  std::vector<player_entry> result{};
  result.reserve(persistent_players.size() + (include_runtime ? runtime_players.size() : 0));

  for (const auto& [account_id, player] : persistent_players)
  {
    result.emplace_back(account_id, player.state, player.name, false);
  }

  if (include_runtime)
  {
    for (const auto& [account_id, player] : runtime_players)
    {
      if (persistent_players.contains(account_id))
      {
        continue;
      }

      result.emplace_back(account_id, player.state, player.name, true);
    }
  }

  std::ranges::sort(result, [](const auto& left, const auto& right)
  {
    return left.account_id < right.account_id;
  });
  return result;
}

std::uint32_t account_id_for_player_index(int player_index)
{
  if (engine == nullptr || player_index <= 0)
  {
    return 0;
  }

  player_info info{};
  if (!engine->get_player_info(player_index, &info) || info.fakeplayer)
  {
    return 0;
  }

  return static_cast<std::uint32_t>(info.friends_id);
}

} // namespace cathook::core::players
