/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_goals.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef NAVBOT_GOALS_HPP
#define NAVBOT_GOALS_HPP

#include <string>

#include "features/automation/navbot/navbot_mesh.hpp"
#include "features/automation/navbot/navbot_types.hpp"

class Player;

namespace navbot
{

class navbot_goals
{
public:
  [[nodiscard]] navbot_goal_state select_goal(const navbot_mesh& mesh, Player* localplayer, float current_time);

private:
  struct cached_flag_home
  {
    bool valid = false;
    Vec3 origin{};
  };

  void reset_flag_home_cache();
  void update_flag_home_cache(tf_team team, const Vec3& origin);
  [[nodiscard]] cached_flag_home flag_home_for_team(tf_team team) const;
  [[nodiscard]] goal_candidate choose_flag_goal(const navbot_mesh& mesh, Player* localplayer);
  [[nodiscard]] goal_candidate choose_roam_goal(const navbot_mesh& mesh, Player* localplayer, float current_time);

  std::string cached_map_name_{};
  cached_flag_home red_flag_home_{};
  cached_flag_home blu_flag_home_{};
  nav_area_id last_roam_area_{};
  float next_roam_refresh_time_ = 0.0f;
  size_t roam_cursor_ = 0;
};

} // namespace navbot

#endif
