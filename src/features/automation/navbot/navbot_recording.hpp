#ifndef NAVBOT_RECORDING_HPP
#define NAVBOT_RECORDING_HPP

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "features/automation/navbot/navbot_types.hpp"

namespace navbot
{

struct server_recording_context
{
  int captured_point_index = -1;
  uint32_t mini_round_mask = 0;
  bool setup_finished = false;
};

struct server_recording_status
{
  bool recording = false;
  bool server_module_found = false;
  bool signature_found = false;
  bool write_ok = false;
  uint32_t server_area_count = 0;
  uint32_t blocked_area_count = 0;
  uint32_t unique_blocked_area_count = 0;
  uint32_t snapshot_count = 0;
  std::string map_name{};
  std::string output_path{};
  std::string message{};
};

class server_nav_recorder
{
public:
  void reset();
  void update(const std::string& map_name, const server_recording_context& context, float current_time);

  [[nodiscard]] const server_recording_status& status() const;
  [[nodiscard]] const std::vector<recorded_blocked_area>& blocked_areas() const;

private:
  bool resolve_server_nav();
  bool read_snapshot(const server_recording_context& context, std::string& snapshot_json, uint32_t& area_count, uint32_t& blocked_count, std::vector<recorded_blocked_area>& blocked_areas);
  bool write_snapshot(const std::string& map_name, const std::string& snapshot_json);
  bool load_recorded_snapshot(const std::string& map_name, const server_recording_context& context);

  server_recording_status status_{};
  std::string last_map_name_{};
  std::string last_snapshot_json_{};
  float next_retry_time_ = 0.0f;
  float next_snapshot_time_ = 0.0f;
  uintptr_t area_vector_address_ = 0;
  std::unordered_set<uint32_t> unique_blocked_area_ids_{};
  std::vector<recorded_blocked_area> blocked_areas_{};
};

}

#endif
