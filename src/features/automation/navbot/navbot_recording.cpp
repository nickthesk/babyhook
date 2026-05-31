#include "features/automation/navbot/navbot_recording.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

#include "core/logger.hpp"
#include "core/shared/sigs.hpp"
#include "libsigscan/libsigscan.h"

namespace navbot
{

namespace
{

constexpr float server_retry_interval = 2.0f;
constexpr float snapshot_interval = 0.5f;
constexpr uint32_t max_reasonable_server_areas = 131072;
constexpr uintptr_t server_area_blocked_red_offset = 0x3e;
constexpr uintptr_t server_area_blocked_blue_offset = 0x3f;
constexpr uintptr_t server_area_id_offset = 0xc0;
constexpr uintptr_t server_area_tf_attributes_offset = 0x29c;
constexpr uintptr_t area_vector_lea_offset = 0x16;
constexpr uintptr_t area_vector_lea_disp_offset = area_vector_lea_offset + 3;
constexpr uintptr_t area_vector_lea_next_offset = area_vector_lea_offset + 7;
constexpr uintptr_t area_vector_count_offset = 0x10;

struct readable_memory_range
{
  uintptr_t begin = 0;
  uintptr_t end = 0;
};

std::string json_escape(const std::string& value)
{
  std::string output{};
  output.reserve(value.size() + 8);
  for (const char c : value)
  {
    switch (c)
    {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        output += c;
        break;
    }
  }
  return output;
}

std::string sanitized_file_stem(const std::string& value)
{
  std::string output{};
  output.reserve(value.size());
  for (const char c : value)
  {
    const bool valid =
      (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '_' ||
      c == '-';
    output += valid ? c : '_';
  }
  return output.empty() ? "unknown_map" : output;
}

bool server_module_loaded()
{
  SigscanModuleBounds* bounds = sigscan_get_module_bounds(SIGSCAN_PID_SELF, "server.so");
  const bool loaded = bounds != nullptr;
  if (bounds != nullptr)
  {
    sigscan_free_module_bounds(bounds);
  }
  return loaded;
}

uintptr_t read_rip_relative_target(uint8_t* instruction, uintptr_t displacement_offset, uintptr_t next_instruction_offset)
{
  int32_t displacement = 0;
  std::memcpy(&displacement, instruction + displacement_offset, sizeof(displacement));
  return reinterpret_cast<uintptr_t>(instruction + next_instruction_offset) + static_cast<intptr_t>(displacement);
}

std::vector<readable_memory_range> read_readable_memory_ranges()
{
  std::vector<readable_memory_range> ranges{};
#if defined(__linux__)
  std::ifstream maps{ "/proc/self/maps" };
  std::string line{};
  while (std::getline(maps, line))
  {
    const size_t dash = line.find('-');
    const size_t space = line.find(' ', dash == std::string::npos ? 0 : dash + 1);
    if (dash == std::string::npos || space == std::string::npos || space + 1 >= line.size() || line[space + 1] != 'r')
    {
      continue;
    }

    uintptr_t begin = 0;
    uintptr_t end = 0;
    const char* begin_text = line.data();
    const char* dash_text = line.data() + dash;
    const char* end_text = line.data() + space;
    const std::from_chars_result begin_result = std::from_chars(begin_text, dash_text, begin, 16);
    const std::from_chars_result end_result = std::from_chars(dash_text + 1, end_text, end, 16);
    if (begin_result.ec == std::errc{} && end_result.ec == std::errc{} && begin < end)
    {
      ranges.emplace_back(readable_memory_range{ begin, end });
    }
  }
#endif
  return ranges;
}

bool readable_ranges_contain(uintptr_t address, size_t size, const std::vector<readable_memory_range>& ranges)
{
#if !defined(__linux__)
  (void)address;
  (void)size;
  (void)ranges;
  return true;
#else
  if (size == 0)
  {
    return true;
  }

  if (address > std::numeric_limits<uintptr_t>::max() - static_cast<uintptr_t>(size))
  {
    return false;
  }

  uintptr_t current = address;
  const uintptr_t end = address + static_cast<uintptr_t>(size);
  for (const readable_memory_range& range : ranges)
  {
    if (current < range.begin)
    {
      continue;
    }

    if (current >= range.end)
    {
      continue;
    }

    if (end <= range.end)
    {
      return true;
    }

    current = range.end;
  }

  return false;
#endif
}

bool read_int_field(const std::string& line, const char* name, int& value)
{
  const std::string key = std::string{"\""} + name + "\":";
  size_t position = line.find(key);
  if (position == std::string::npos)
  {
    return false;
  }

  position += key.size();
  const char* begin = line.data() + position;
  const char* end = line.data() + line.size();
  const std::from_chars_result result = std::from_chars(begin, end, value);
  return result.ec == std::errc{};
}

bool read_uint_field(const std::string& line, const char* name, uint32_t& value)
{
  int parsed = 0;
  if (!read_int_field(line, name, parsed) || parsed < 0)
  {
    return false;
  }

  value = static_cast<uint32_t>(parsed);
  return true;
}

bool read_bool_field(const std::string& line, const char* name, bool& value)
{
  const std::string key = std::string{"\""} + name + "\":";
  size_t position = line.find(key);
  if (position == std::string::npos)
  {
    return false;
  }

  position += key.size();
  if (line.compare(position, 4, "true") == 0)
  {
    value = true;
    return true;
  }
  if (line.compare(position, 5, "false") == 0)
  {
    value = false;
    return true;
  }

  return false;
}

bool snapshot_context_matches(const std::string& line, const server_recording_context& context)
{
  int captured_point_index = -1;
  uint32_t mini_round_mask = 0;
  bool setup_finished = false;
  return read_int_field(line, "captured_point", captured_point_index) &&
         read_uint_field(line, "mini_round_mask", mini_round_mask) &&
         read_bool_field(line, "setup_finished", setup_finished) &&
         captured_point_index == context.captured_point_index &&
         mini_round_mask == context.mini_round_mask &&
         setup_finished == context.setup_finished;
}

std::vector<recorded_blocked_area> parse_blocked_areas(const std::string& line)
{
  std::vector<recorded_blocked_area> areas{};
  size_t position = 0;
  while (true)
  {
    position = line.find("\"id\":", position);
    if (position == std::string::npos)
    {
      break;
    }

    position += 5;
    uint32_t area_id = 0;
    const char* begin = line.data() + position;
    const char* end = line.data() + line.size();
    const std::from_chars_result parse_result = std::from_chars(begin, end, area_id);
    if (parse_result.ec != std::errc{})
    {
      break;
    }

    const size_t object_end = line.find('}', position);
    if (object_end == std::string::npos)
    {
      break;
    }

    const std::string object = line.substr(position, object_end - position);
    bool red = false;
    bool blue = false;
    read_bool_field(object, "red", red);
    read_bool_field(object, "blue", blue);
    areas.emplace_back(recorded_blocked_area{ nav_area_id{ area_id }, red, blue });
    position = object_end + 1;
  }

  std::sort(areas.begin(), areas.end(), [](const recorded_blocked_area& left, const recorded_blocked_area& right)
  {
    return left.area_id.value < right.area_id.value;
  });
  areas.erase(std::unique(areas.begin(), areas.end(), [](const recorded_blocked_area& left, const recorded_blocked_area& right)
  {
    return left.area_id.value == right.area_id.value;
  }), areas.end());
  return areas;
}

}

void server_nav_recorder::reset()
{
  status_ = {};
  last_map_name_.clear();
  last_snapshot_json_.clear();
  next_retry_time_ = 0.0f;
  next_snapshot_time_ = 0.0f;
  area_vector_address_ = 0;
  unique_blocked_area_ids_.clear();
  blocked_areas_.clear();
}

void server_nav_recorder::update(const std::string& map_name, const server_recording_context& context, float current_time)
{
  status_.recording = false;
  status_.map_name = map_name;

  if (map_name.empty())
  {
    status_.message = "no map";
    return;
  }

  if (map_name != last_map_name_)
  {
    last_map_name_ = map_name;
    last_snapshot_json_.clear();
    unique_blocked_area_ids_.clear();
    blocked_areas_.clear();
    status_.snapshot_count = 0;
    status_.unique_blocked_area_count = 0;
    next_snapshot_time_ = 0.0f;
  }

  if (area_vector_address_ == 0 && current_time >= next_retry_time_)
  {
    next_retry_time_ = current_time + server_retry_interval;
    resolve_server_nav();
  }
  else if (area_vector_address_ != 0 && current_time >= next_snapshot_time_ && !server_module_loaded())
  {
    area_vector_address_ = 0;
    status_.server_module_found = false;
    status_.signature_found = false;
    blocked_areas_.clear();
  }

  if (area_vector_address_ == 0)
  {
    if (load_recorded_snapshot(map_name, context))
    {
      return;
    }

    if (status_.message.empty())
    {
      status_.message = status_.server_module_found ? "server signature missing" : "server.so not loaded";
    }
    return;
  }

  status_.recording = true;
  if (current_time < next_snapshot_time_)
  {
    status_.message = "recording";
    return;
  }
  next_snapshot_time_ = current_time + snapshot_interval;

  std::string snapshot_json{};
  uint32_t area_count = 0;
  uint32_t blocked_count = 0;
  std::vector<recorded_blocked_area> blocked_areas{};
  if (!read_snapshot(context, snapshot_json, area_count, blocked_count, blocked_areas))
  {
    area_vector_address_ = 0;
    status_.signature_found = false;
    blocked_areas_.clear();
    status_.message = "snapshot read failed";
    return;
  }

  blocked_areas_ = std::move(blocked_areas);
  status_.server_area_count = area_count;
  status_.blocked_area_count = blocked_count;
  status_.unique_blocked_area_count = static_cast<uint32_t>(unique_blocked_area_ids_.size());

  if (snapshot_json == last_snapshot_json_)
  {
    status_.message = "recording unchanged";
    return;
  }

  if (write_snapshot(map_name, snapshot_json))
  {
    last_snapshot_json_ = snapshot_json;
    ++status_.snapshot_count;
    status_.write_ok = true;
    status_.message = "recorded";
  }
  else
  {
    status_.write_ok = false;
    status_.message = "write failed";
  }
}

const server_recording_status& server_nav_recorder::status() const
{
  return status_;
}

const std::vector<recorded_blocked_area>& server_nav_recorder::blocked_areas() const
{
  return blocked_areas_;
}

bool server_nav_recorder::resolve_server_nav()
{
  status_.server_module_found = server_module_loaded();
  status_.signature_found = false;
  area_vector_address_ = 0;

  if (!status_.server_module_found)
  {
    return false;
  }

  auto* match = reinterpret_cast<uint8_t*>(sigscan_module("server.so", sigs::navbot_server_compute_blocked_areas));
  if (match == nullptr)
  {
    return false;
  }

  area_vector_address_ = read_rip_relative_target(match, area_vector_lea_disp_offset, area_vector_lea_next_offset);
  status_.signature_found = area_vector_address_ != 0;
  return status_.signature_found;
}

bool server_nav_recorder::read_snapshot(const server_recording_context& context, std::string& snapshot_json, uint32_t& area_count, uint32_t& blocked_count, std::vector<recorded_blocked_area>& blocked_areas)
{
  if (area_vector_address_ == 0)
  {
    return false;
  }

  const std::vector<readable_memory_range> readable_ranges = read_readable_memory_ranges();
  if (!readable_ranges_contain(area_vector_address_, area_vector_count_offset + sizeof(int), readable_ranges))
  {
    return false;
  }

  auto** areas = *reinterpret_cast<uintptr_t***>(area_vector_address_);
  const int raw_area_count = *reinterpret_cast<int*>(area_vector_address_ + area_vector_count_offset);
  if (areas == nullptr || raw_area_count < 0 || static_cast<uint32_t>(raw_area_count) > max_reasonable_server_areas)
  {
    return false;
  }

  area_count = static_cast<uint32_t>(raw_area_count);
  const uintptr_t areas_address = reinterpret_cast<uintptr_t>(areas);
  const size_t area_pointer_bytes = static_cast<size_t>(area_count) * sizeof(uintptr_t*);
  if (!readable_ranges_contain(areas_address, area_pointer_bytes, readable_ranges))
  {
    return false;
  }

  blocked_count = 0;

  std::ostringstream stream{};
  stream << "{\"captured_point\":" << context.captured_point_index
         << ",\"mini_round_mask\":" << context.mini_round_mask
         << ",\"setup_finished\":" << (context.setup_finished ? "true" : "false")
         << ",\"areas\":[";

  bool first_area = true;
  for (uint32_t index = 0; index < area_count; ++index)
  {
    auto* area = reinterpret_cast<uint8_t*>(areas[index]);
    if (area == nullptr)
    {
      continue;
    }

    const uintptr_t area_address = reinterpret_cast<uintptr_t>(area);
    if (!readable_ranges_contain(area_address, server_area_tf_attributes_offset + sizeof(uint32_t), readable_ranges))
    {
      continue;
    }

    const bool red_blocked = *(area + server_area_blocked_red_offset) != 0;
    const bool blue_blocked = *(area + server_area_blocked_blue_offset) != 0;
    if (!red_blocked && !blue_blocked)
    {
      continue;
    }

    const uint32_t area_id = *reinterpret_cast<uint32_t*>(area + server_area_id_offset);
    const uint32_t tf_attributes = *reinterpret_cast<uint32_t*>(area + server_area_tf_attributes_offset);
    if (!first_area)
    {
      stream << ',';
    }
    first_area = false;
    stream << "{\"id\":" << area_id
           << ",\"red\":" << (red_blocked ? "true" : "false")
           << ",\"blue\":" << (blue_blocked ? "true" : "false")
           << ",\"tf\":" << tf_attributes
           << '}';
    ++blocked_count;
    unique_blocked_area_ids_.insert(area_id);
    blocked_areas.emplace_back(recorded_blocked_area{ nav_area_id{ area_id }, red_blocked, blue_blocked });
  }

  stream << "]}";
  snapshot_json = stream.str();
  std::sort(blocked_areas.begin(), blocked_areas.end(), [](const recorded_blocked_area& left, const recorded_blocked_area& right)
  {
    return left.area_id.value < right.area_id.value;
  });
  return true;
}

bool server_nav_recorder::write_snapshot(const std::string& map_name, const std::string& snapshot_json)
{
  std::error_code error{};
  const std::filesystem::path directory = cathook::core::config_directory() / "navbot_recordings";
  std::filesystem::create_directories(directory, error);
  if (error)
  {
    return false;
  }

  const std::filesystem::path output_path = directory / (sanitized_file_stem(map_name) + ".jsonl");
  status_.output_path = output_path.string();
  std::ofstream output{ output_path, std::ios::app };
  if (!output)
  {
    return false;
  }

  output << "{\"map\":\"" << json_escape(map_name) << "\",\"snapshot\":" << snapshot_json << "}\n";
  return static_cast<bool>(output);
}

bool server_nav_recorder::load_recorded_snapshot(const std::string& map_name, const server_recording_context& context)
{
  const std::filesystem::path output_path = cathook::core::config_directory() / "navbot_recordings" / (sanitized_file_stem(map_name) + ".jsonl");
  status_.output_path = output_path.string();

  std::ifstream input{ output_path };
  if (!input)
  {
    blocked_areas_.clear();
    status_.message = "recording file missing";
    return false;
  }

  std::string line{};
  std::vector<recorded_blocked_area> matched_areas{};
  uint32_t matched_snapshots = 0;
  while (std::getline(input, line))
  {
    if (!snapshot_context_matches(line, context))
    {
      continue;
    }

    matched_areas = parse_blocked_areas(line);
    ++matched_snapshots;
  }

  if (matched_snapshots == 0)
  {
    blocked_areas_.clear();
    status_.blocked_area_count = 0;
    status_.message = "no matching recording";
    return false;
  }

  blocked_areas_ = std::move(matched_areas);
  status_.blocked_area_count = static_cast<uint32_t>(blocked_areas_.size());
  status_.unique_blocked_area_count = status_.blocked_area_count;
  status_.snapshot_count = matched_snapshots;
  status_.write_ok = true;
  status_.message = "replaying recording";
  return true;
}

}
