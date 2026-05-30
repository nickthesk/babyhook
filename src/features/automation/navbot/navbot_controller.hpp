/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_controller.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef NAVBOT_CONTROLLER_HPP
#define NAVBOT_CONTROLLER_HPP

#include <string>

#include "features/automation/navbot/navbot_debug.hpp"
#include "features/automation/navbot/navbot_follow.hpp"
#include "features/automation/navbot/navbot_goals.hpp"
#include "features/automation/navbot/navbot_hazards.hpp"
#include "features/automation/navbot/navbot_jobs.hpp"
#include "features/automation/navbot/navbot_mesh.hpp"
#include "features/automation/navbot/navbot_types.hpp"

struct user_cmd;
class GameEvent;

namespace navbot
{

class navbot_controller
{
public:
  void on_create_move(user_cmd* user_cmd);
  void on_frame_stage_notify();
  void on_game_event(GameEvent* event);
  void draw_imgui();

  [[nodiscard]] const navbot_debug_state& debug_state() const;
  [[nodiscard]] bool should_suppress_aimbot() const;
  [[nodiscard]] bool should_prioritize_danger_movement() const;

private:
  struct crumb_failure_state
  {
    nav_area_id area_id{};
    nav_edge_id edge_id{};
    uint32_t count = 0;
    float last_failure_time = 0.0f;
  };

  void ensure_started();
  void rebuild_mesh_if_needed();
  void poll_path_results();
  void request_path_if_needed();
  void update_hazards();
  void clear_runtime_state();
  void update_weapon_choice(Player* localplayer);
  bool record_crumb_failure(const follower_tick_result& follow_result, float current_time);
  [[nodiscard]] bool should_block_pathing(Player* localplayer) const;

  navbot_mesh mesh_{};
  navbot_hazards hazards_{};
  navbot_goals goals_{};
  navbot_follow follower_{};
  navbot_jobs jobs_{};

  bool jobs_started_ = false;
  std::string loaded_map_name_{};
  navbot_goal_state active_goal_{};
  navbot_debug_state debug_state_{};
  job_handle pending_job_{};
  path_result active_path_{};
  uint64_t next_request_id_ = 1;
  uint32_t current_generation_id_ = 0;
  uint32_t world_generation_id_ = 0;
  float next_goal_refresh_time_ = 0.0f;
  float next_path_request_time_ = 0.0f;
  float next_hazard_update_time_ = 0.0f;
  float next_weapon_switch_time_ = 0.0f;
  int last_requested_weapon_slot_ = 0;
  int pending_desired_weapon_slot_ = 0;
  float pending_desired_since_ = 0.0f;
  crumb_failure_state crumb_failure_{};
  bool suppress_aimbot_for_reload_ = false;
  bool round_started_ = false;
  bool setup_finished_ = false;
  bool warmup_active_ = false;
};

navbot_controller& controller();

} // namespace navbot

#endif
