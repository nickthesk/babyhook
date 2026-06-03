/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_path.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef NAVBOT_PATH_HPP
#define NAVBOT_PATH_HPP

#include <vector>

#include "features/automation/navbot/navbot_hazards.hpp"
#include "features/automation/navbot/navbot_mesh.hpp"
#include "features/automation/navbot/navbot_types.hpp"

namespace navbot
{

struct transition_points
{
  Vec3 current{};
  Vec3 center{};
  Vec3 center_next{};
  Vec3 next{};
};

transition_points build_transition_points(const navbot_mesh& mesh, nav_area_id current_area, nav_area_id next_area, const path_clearance& clearance);
Vec3 apply_dropdown_adjustment(const Vec3& current_pos, const Vec3& next_pos, const path_clearance& clearance);
bool nav_area_has_clearance(const nav_area_data& area, const path_clearance& clearance);
bool nav_transition_has_clearance(const nav_area_data& from_area, const nav_area_data& to_area, const path_clearance& clearance);
Vec3 clamp_point_to_player_clearance(const nav_area_data& area, const Vec3& point, const path_clearance& clearance);
std::vector<crumb> build_crumbs_from_area_path(const navbot_mesh& mesh, const std::vector<nav_area_id>& area_path, const Vec3& destination, const path_clearance& clearance);
path_result solve_path_request(const navbot_mesh& mesh, const navbot_hazards& hazards, const path_request& request, const cancellation_token& token, float current_time);

} // namespace navbot

#endif
