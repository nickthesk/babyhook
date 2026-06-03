/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_follow.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef NAVBOT_FOLLOW_HPP
#define NAVBOT_FOLLOW_HPP

#include <vector>

#include "features/automation/navbot/navbot_mesh.hpp"
#include "features/automation/navbot/navbot_types.hpp"

struct user_cmd;
class Player;

namespace navbot
{

class navbot_follow
{
public:
  void clear();
  void set_path(path_result path);

  [[nodiscard]] bool has_path() const;
  [[nodiscard]] const std::vector<crumb>& crumbs() const;
  [[nodiscard]] const crumb* current_crumb() const;
  [[nodiscard]] uint32_t generation_id() const;
  [[nodiscard]] size_t current_crumb_index() const;
  [[nodiscard]] const std::vector<float>& reached_crumb_times() const;
  [[nodiscard]] bool is_stuck(float current_time) const;

  follower_tick_result tick(const navbot_mesh& mesh, Player* localplayer, user_cmd* user_cmd, float current_time);

private:
  void mark_crumb_reached(size_t crumb_index, float current_time);
  void advance_to_crumb(size_t crumb_index, float current_time);
  [[nodiscard]] size_t find_skip_ahead_crumb(Player* localplayer, const Vec3& local_origin) const;
  bool try_unstuck(Player* localplayer, user_cmd* user_cmd, const crumb& current_crumb, float current_time, follower_tick_result& result);

  path_result active_path_{};
  std::vector<float> reached_crumb_times_{};
  size_t current_crumb_index_ = 0;
  float current_crumb_start_time_ = 0.0f;
  float last_progress_time_ = 0.0f;
  float last_progress_distance_sq_ = 0.0f;
  float last_vischeck_time_ = 0.0f;
  float last_jump_time_ = 0.0f;
  bool duck_jump_active_ = false;
  int duck_jump_ticks_ = 0;
};

} // namespace navbot

#endif
