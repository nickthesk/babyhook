#include "core/identify/identify.hpp"

#include "core/identify/identify_client.hpp"

#include "core/commands.hpp"
#include "core/player_manager.hpp"
#include "core/print.hpp"

#include "external/MD5/MD5.hpp"

#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/net_channel.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cathook::core::identify
{
namespace
{

constexpr const char* SERVER_HOST      = "identify.bigmaccasnetwork.party";
constexpr int         SERVER_PORT      = 3000;
constexpr int         BETRAYAL_LIMIT   = 2;
constexpr int         TICK_INTERVAL_MS = 1000;

IdentifyClient* client = nullptr;

// mutexs + lock guard to prevent desync
std::mutex                      peer_mu;
std::unordered_set<std::string> peer_hashes;

std::mutex                        identified_mu;
std::unordered_set<std::uint32_t> identified_account_ids;

std::mutex                             betrayals_mu;
std::unordered_map<std::uint32_t, int> betrayals;

std::mutex                                       chat_mu;
std::vector<std::pair<std::string, std::string>> chat_queue;

std::string player_hash(std::uint32_t friends_id, const char* name)
{
  if (friends_id == 0 || name == nullptr || name[0] == '\0')
    return "";

  std::string blob = std::to_string(friends_id) + name;
  MD5Value_t result{};
  MD5_ProcessSingleBuffer(blob.data(), static_cast<int>(blob.size()), result);

  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (unsigned char b : result.bits)
    ss << std::setw(2) << static_cast<int>(b);
  return ss.str();
}

// netchannel address as our group key. empty when not on a server.
std::string current_server_id()
{
  if (engine == nullptr || !engine->is_in_game())
    return "";
  if (client_state == nullptr || client_state->m_NetChannel == nullptr)
    return "";
  const char* addr = client_state->m_NetChannel->get_address();
  if (addr == nullptr || *addr == '\0')
    return "";
  return std::string(addr);
}

std::string local_player_hash()
{
  if (engine == nullptr)
    return "";
  int local_idx = engine->get_localplayer_index();
  if (local_idx <= 0)
    return "";
  player_info info{};
  if (!engine->get_player_info(local_idx, &info) || info.friends_id == 0)
    return "";
  return player_hash(static_cast<std::uint32_t>(info.friends_id), info.name);
}

void on_peers(const std::vector<std::string>& hashes)
{
  std::lock_guard lk{peer_mu};
  peer_hashes.clear();
  for (const std::string& h : hashes)
    peer_hashes.insert(h);
}

void on_chat(const std::string& from, const std::string& msg)
{
  std::lock_guard lk{chat_mu};
  chat_queue.emplace_back(from, msg);
}

void rebuild_identified_set()
{
  if (engine == nullptr || entity_list == nullptr || !engine->is_in_game())
  {
    std::lock_guard lk{identified_mu};
    identified_account_ids.clear();
    return;
  }

  std::unordered_set<std::string> peers_snapshot;
  {
    std::lock_guard lk{peer_mu};
    peers_snapshot = peer_hashes;
  }

  std::unordered_set<std::uint32_t> next;
  if (!peers_snapshot.empty())
  {
    int max_entities = entity_list->get_max_entities();
    int local_idx    = engine->get_localplayer_index();
    for (int i = 1; i <= max_entities; ++i)
    {
      if (i == local_idx)
        continue;

      Player* p = entity_list->player_from_index(static_cast<unsigned int>(i));
      if (p == nullptr || p->get_class_id() != class_id::PLAYER)
        continue;

      player_info info{};
      if (!engine->get_player_info(i, &info) || info.fakeplayer || info.friends_id == 0)
        continue;

      std::uint32_t account_id = static_cast<std::uint32_t>(info.friends_id);
      {
        std::lock_guard blk{betrayals_mu};
        auto bit = betrayals.find(account_id);
        if (bit != betrayals.end() && bit->second >= BETRAYAL_LIMIT)
          continue;
      }

      std::string h = player_hash(account_id, info.name);
      if (!peers_snapshot.count(h))
        continue;

      next.insert(account_id);
    }
  }

  std::lock_guard lk{identified_mu};
  identified_account_ids = std::move(next);
}

void drain_chat()
{
  std::vector<std::pair<std::string, std::string>> drained;
  {
    std::lock_guard lk{chat_mu};
    drained.swap(chat_queue);
  }
  for (const auto& [from, msg] : drained)
    print("[identify %.6s] %s\n", from.c_str(), msg.c_str());
}

void command_identify_send_callback(const cathook::core::command_args& args)
{
  if (client == nullptr)
    return;
  if (args.argc() < 2)
  {
    print("usage: cat_identify_send <message>\n");
    return;
  }
  std::string msg;
  for (int i = 1; i < args.argc(); ++i)
  {
    if (i > 1)
      msg += ' ';
    msg += args.argv(i);
  }
  client->sendChat(msg);
}

void command_clear_betrayals_callback(const cathook::core::command_args&)
{
  clear_betrayals();
  print("[identify] betrayals cleared\n");
}

} // namespace

void start()
{
  {
    std::lock_guard lk{betrayals_mu};
    betrayals.clear();
  }

  client = new IdentifyClient();
  client->setPeersHandler(on_peers);
  client->setChatHandler(on_chat);
  client->connect(SERVER_HOST, SERVER_PORT);

  cathook::core::add_command(
      "cat_identify_send",
      command_identify_send_callback,
      "Send a chat message to others on the same server");
  cathook::core::add_command(
      "cat_clear_betrayals",
      command_clear_betrayals_callback,
      "Clear the identify betrayal log");

  print("[identify] init done, connecting to %s:%d\n", SERVER_HOST, SERVER_PORT);
}

void stop()
{
  if (client != nullptr)
  {
    client->stop();
    delete client;
    client = nullptr;
  }
  {
    std::lock_guard lk{identified_mu};
    identified_account_ids.clear();
  }
  {
    std::lock_guard lk{peer_mu};
    peer_hashes.clear();
  }
}

void tick()
{
  if (client == nullptr)
    return;

  // cooldown
  using clock = std::chrono::steady_clock;
  static clock::time_point last_run{};
  clock::time_point now = clock::now();
  long dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_run).count();
  if (dt < TICK_INTERVAL_MS)
    return;
  last_run = now;

  client->updateIdentity(current_server_id(), local_player_hash());
  rebuild_identified_set();
  drain_chat();
}

bool is_peer(std::uint32_t account_id, std::string_view /*name*/)
{
  if (account_id == 0)
    return false;
  std::lock_guard lk{identified_mu};
  return identified_account_ids.contains(account_id);
}

void on_player_death(int attacker_user_id)
{
  if (engine == nullptr || entity_list == nullptr)
    return;
  int attacker_index = engine->get_player_index_from_id(attacker_user_id);
  if (attacker_index <= 0)
    return;

  player_info info{};
  if (!engine->get_player_info(attacker_index, &info) || info.friends_id == 0)
    return;
  std::uint32_t account_id = static_cast<std::uint32_t>(info.friends_id);

  {
    std::lock_guard lk{identified_mu};
    if (!identified_account_ids.contains(account_id))
      return;
  }

  int count = 0;
  {
    std::lock_guard lk{betrayals_mu};
    count = ++betrayals[account_id];
  }
  if (count >= BETRAYAL_LIMIT)
    print("[identify] %u betrayed %d times; no longer identifying\n", account_id, count);
}

void clear_betrayals()
{
  std::lock_guard lk{betrayals_mu};
  betrayals.clear();
}

} // namespace cathook::core::identify
