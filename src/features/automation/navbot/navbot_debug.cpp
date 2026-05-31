/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_debug.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "features/automation/navbot/navbot_debug.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "imgui/dearimgui.hpp"

#include "features/visuals/overlay_projection.hpp"

namespace navbot
{

namespace
{

const char* goal_type_name(goal_type type)
{
  switch (type)
  {
    case goal_type::get_health:
      return "get_health";
    case goal_type::get_ammo:
      return "get_ammo";
    case goal_type::capture_objective:
      return "capture_objective";
    case goal_type::push_payload:
      return "push_payload";
    case goal_type::defend_payload:
      return "defend_payload";
    case goal_type::get_flag:
      return "get_flag";
    case goal_type::return_flag:
      return "return_flag";
    case goal_type::escape_danger:
      return "escape_danger";
    case goal_type::hold_range_on_enemy:
      return "hold_range_on_enemy";
    case goal_type::melee_chase:
      return "melee_chase";
    case goal_type::sentry_snipe:
      return "sentry_snipe";
    case goal_type::engineer_build:
      return "engineer_build";
    case goal_type::engineer_maintain:
      return "engineer_maintain";
    case goal_type::reload_weapons:
      return "reload_weapons";
    case goal_type::heal_follow:
      return "heal_follow";
    case goal_type::roam:
    default:
      return "roam";
  }
}

const char* path_status_name(path_status status)
{
  switch (status)
  {
    case path_status::success:
      return "success";
    case path_status::no_start_area:
      return "no_start_area";
    case path_status::no_goal_area:
      return "no_goal_area";
    case path_status::no_path:
      return "no_path";
    case path_status::canceled:
      return "canceled";
    case path_status::stale:
      return "stale";
    case path_status::failed:
    default:
      return "failed";
  }
}

const char* failure_reason_name(follower_failure_reason reason)
{
  switch (reason)
  {
    case follower_failure_reason::blocked:
      return "blocked";
    case follower_failure_reason::no_progress:
      return "no_progress";
    case follower_failure_reason::invalid_local_area:
      return "invalid_local_area";
    case follower_failure_reason::destination_invalid:
      return "destination_invalid";
    case follower_failure_reason::stale_path:
      return "stale_path";
    case follower_failure_reason::hazard_intersection:
      return "hazard_intersection";
    case follower_failure_reason::none:
    default:
      return "none";
  }
}

void draw_text_line(ImDrawList* draw_list, const ImVec2& position, const std::string& text)
{
  if (draw_list == nullptr || text.empty())
  {
    return;
  }

  draw_list->AddText(ImVec2(position.x + 1.0f, position.y + 1.0f), IM_COL32(0, 0, 0, 255), text.c_str());
  draw_list->AddText(position, IM_COL32(255, 255, 255, 255), text.c_str());
}

int path_alpha_for_crumb(size_t crumb_index, size_t current_crumb_index, float current_time, const std::vector<float>& reached_crumb_times)
{
  if (crumb_index >= current_crumb_index)
  {
    return 255;
  }

  if (crumb_index >= reached_crumb_times.size())
  {
    return 64;
  }

  auto reached_time = reached_crumb_times[crumb_index];
  if (reached_time <= 0.0f)
  {
    return 96;
  }

  auto elapsed = current_time - reached_time;
  if (elapsed >= passed_crumb_fade_time)
  {
    return 0;
  }

  auto t = 1.0f - (elapsed / passed_crumb_fade_time);
  return static_cast<int>(48.0f + (207.0f * t));
}

float path_node_radius_for_crumb(const path_result& path, size_t crumb_index, size_t current_crumb_index)
{
  if (crumb_index < current_crumb_index)
  {
    return 2.0f;
  }

  const auto ahead_count = crumb_index > current_crumb_index ? crumb_index - current_crumb_index : 0;
  const auto distance_t = std::min(static_cast<float>(ahead_count) / 18.0f, 1.0f);
  auto radius = 5.5f - (3.75f * distance_t);

  if (crumb_index == current_crumb_index)
  {
    radius = 6.25f;
  }
  else if (crumb_index < path.crumbs.size() && path.crumbs[crumb_index].kind == crumb_kind::destination)
  {
    radius += 1.25f;
  }

  return std::clamp(radius, 1.75f, 6.25f);
}

bool should_draw_path_node(const path_result& path, size_t crumb_index, size_t current_crumb_index)
{
  if (crumb_index <= current_crumb_index || crumb_index + 1 >= path.crumbs.size())
  {
    return true;
  }

  if (path.crumbs[crumb_index].kind == crumb_kind::destination)
  {
    return true;
  }

  const auto ahead_count = crumb_index - current_crumb_index;
  if (ahead_count <= 8)
  {
    return true;
  }

  if (ahead_count <= 18)
  {
    return ahead_count % 2 == 0;
  }

  return ahead_count % 4 == 0;
}

} // namespace

void draw_path_exact_imgui(ImDrawList* draw_list, const path_result& path, size_t current_crumb_index, float current_time, const std::vector<float>& reached_crumb_times)
{
  if (draw_list == nullptr || path.crumbs.empty())
  {
    return;
  }

  if (!overlay_projection::begin_frame())
  {
    return;
  }

  for (size_t crumb_index = 0; crumb_index < path.crumbs.size(); ++crumb_index)
  {
    if (crumb_index + 1 >= path.crumbs.size())
    {
      continue;
    }

    Vec3 start_screen{};
    const auto start_world = path.crumbs[crumb_index].world;
    if (!overlay_projection::world_to_screen(start_world, &start_screen))
    {
      continue;
    }

    Vec3 end_screen{};
    const auto end_world = path.crumbs[crumb_index + 1].world;
    if (!overlay_projection::world_to_screen(end_world, &end_screen))
    {
      continue;
    }

    auto alpha = path_alpha_for_crumb(crumb_index, current_crumb_index, current_time, reached_crumb_times);
    auto next_alpha = path_alpha_for_crumb(crumb_index + 1, current_crumb_index, current_time, reached_crumb_times);
    auto line_alpha = std::min(alpha, next_alpha);
    if (line_alpha <= 0)
    {
      continue;
    }

    const auto start_radius = path_node_radius_for_crumb(path, crumb_index, current_crumb_index);
    const auto end_radius = path_node_radius_for_crumb(path, crumb_index + 1, current_crumb_index);
    const auto line_width = std::clamp(std::min(start_radius, end_radius) * 0.55f, 1.0f, 2.25f);
    const auto line_color = crumb_index < current_crumb_index
      ? IM_COL32(130, 210, 255, line_alpha)
      : IM_COL32(255, 245, 255, line_alpha);
    draw_list->AddLine(ImVec2(start_screen.x, start_screen.y), ImVec2(end_screen.x, end_screen.y), IM_COL32(0, 0, 0, line_alpha), line_width + 1.5f);
    draw_list->AddLine(ImVec2(start_screen.x, start_screen.y), ImVec2(end_screen.x, end_screen.y), line_color, line_width);
  }

  for (size_t crumb_index = 0; crumb_index < path.crumbs.size(); ++crumb_index)
  {
    if (!should_draw_path_node(path, crumb_index, current_crumb_index))
    {
      continue;
    }

    Vec3 start_screen{};
    const auto start_world = path.crumbs[crumb_index].world;
    if (!overlay_projection::world_to_screen(start_world, &start_screen))
    {
      continue;
    }

    auto alpha = path_alpha_for_crumb(crumb_index, current_crumb_index, current_time, reached_crumb_times);
    if (alpha <= 0)
    {
      continue;
    }

    const auto node_radius = path_node_radius_for_crumb(path, crumb_index, current_crumb_index);
    const auto node_color = crumb_index < current_crumb_index
      ? IM_COL32(120, 200, 255, alpha)
      : crumb_index == current_crumb_index
        ? IM_COL32(145, 255, 170, alpha)
        : IM_COL32(255, 235, 255, alpha);
    const auto center = ImVec2(start_screen.x, start_screen.y);
    draw_list->AddCircleFilled(center, node_radius + 1.25f, IM_COL32(0, 0, 0, alpha), 12);
    draw_list->AddCircleFilled(center, node_radius, node_color, 12);

    if (crumb_index == current_crumb_index)
    {
      draw_list->AddCircle(center, node_radius + 2.5f, IM_COL32(145, 255, 170, alpha), 18, 1.5f);
    }
  }
}

void draw_debug_overlay_imgui(ImDrawList* draw_list, const navbot_debug_state& debug_state)
{
  if (draw_list == nullptr)
  {
    return;
  }

  auto lines = std::vector<std::string>{};
  lines.reserve(18);
  lines.emplace_back("navbot");
  lines.emplace_back(std::string("map: ") + debug_state.map_name);
  lines.emplace_back(std::string("mesh: ") + (debug_state.mesh_ready ? "ready" : "missing"));
  lines.emplace_back(std::string("goal: ") + (debug_state.goal_valid ? goal_type_name(debug_state.current_goal) : "none"));
  lines.emplace_back(std::string("path: ") + path_status_name(debug_state.current_path_status));
  if (!debug_state.path_request_message.empty())
  {
    lines.emplace_back(std::string("path_request: ") + debug_state.path_request_message);
  }
  lines.emplace_back(std::string("active_path: ") + (debug_state.has_active_path ? "yes" : "no"));
  lines.emplace_back(std::string("crumbs: ") + std::to_string(debug_state.active_crumb_count));
  lines.emplace_back(std::string("last_fail: ") + failure_reason_name(debug_state.last_failure));
  lines.emplace_back(std::string("cp: ") + std::to_string(debug_state.captured_point_index) +
                     " setup: " + (debug_state.setup_finished ? "done" : "active") +
                     " mini: " + std::to_string(debug_state.mini_round_mask));
  lines.emplace_back(std::string("recording: ") +
                     (debug_state.server_recording ? "yes" : "no") +
                     " server: " + (debug_state.server_module_found ? "yes" : "no") +
                     " sig: " + (debug_state.server_signature_found ? "yes" : "no"));
  lines.emplace_back(std::string("recorded: snapshots ") + std::to_string(debug_state.server_recorded_snapshots) +
                     " blocked " + std::to_string(debug_state.server_recorded_blocked_areas) +
                     " unique " + std::to_string(debug_state.server_recorded_unique_areas) +
                     " total " + std::to_string(debug_state.server_recorded_total_areas));
  if (!debug_state.server_recording_message.empty())
  {
    lines.emplace_back(std::string("record_status: ") + debug_state.server_recording_message);
  }
  if (!debug_state.server_recording_path.empty())
  {
    lines.emplace_back(std::string("record_file: ") + debug_state.server_recording_path);
  }
  if (!debug_state.nav_file_path.empty())
  {
    lines.emplace_back(std::string("nav: ") + debug_state.nav_file_path);
  }
  else
  {
    lines.emplace_back("nav: not found");
  }

  auto y = 120.0f;
  auto line_height = ImGui::GetTextLineHeight();
  for (const auto& line : lines)
  {
    draw_text_line(draw_list, ImVec2(20.0f, y), line);
    y += line_height;
  }
}

} // namespace navbot
