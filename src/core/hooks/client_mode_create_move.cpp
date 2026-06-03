/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/client_mode_create_move.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"

#include "features/menu/binds.hpp"

#include "games/tf2/sdk/entities/player.hpp"

#include "imgui/dearimgui.hpp"

#include "features/menu/config.hpp"

#include "features/combat/backtrack/backtrack.hpp"
#include "features/combat/aimbot/aimbot.cpp"
#include "features/combat/random_crits/crit_hack.hpp"
#include "features/movement/bhop/bhop.cpp"
#include "features/movement/engine_prediction/engine_prediction.cpp"
#include "features/automation/medic_automation/medic_automation.hpp"
#include "features/automation/misc/misc.hpp"
#include "features/automation/navbot/navbot_controller.hpp"
#include "features/visuals/esp/esp.hpp"
#include "core/detach.hpp"

bool (*client_mode_create_move_original)(void*, float, user_cmd*);
bool g_client_create_move_owns_features = false;

static void movement_fix(user_cmd* user_cmd, Vec3 original_view_angle, float original_forward_move, float original_side_move) {
  if (user_cmd == nullptr) {
    return;
  }

  const bool original_pitch_oob = std::fabs(std::remainder(original_view_angle.x, 360.0f)) > 90.0f;
  const bool current_pitch_oob = std::fabs(std::remainder(user_cmd->view_angles.x, 360.0f)) > 90.0f;
  const float side_move = original_side_move * (original_pitch_oob ? -1.0f : 1.0f);
  const float move_speed = std::hypot(original_forward_move, side_move);
  if (move_speed <= 0.001f) {
    user_cmd->forwardmove = 0.0f;
    user_cmd->sidemove = 0.0f;
    return;
  }

  const float move_yaw = std::atan2(side_move, original_forward_move) * radpi;
  const float original_yaw = original_view_angle.y + (original_pitch_oob ? 180.0f : 0.0f);
  const float current_yaw = user_cmd->view_angles.y + (current_pitch_oob ? 180.0f : 0.0f);
  const float yaw = (current_yaw - original_yaw + move_yaw) * pideg;

  user_cmd->forwardmove = std::cos(yaw) * move_speed;
  user_cmd->sidemove = std::sin(yaw) * move_speed * (current_pitch_oob ? -1.0f : 1.0f);
}

static bool should_block_menu_movement() {
  return ImGui::IsImGuiFullyInitialized() &&
         ImGui::IsAnyItemActive() &&
         !ImGui::IsMouseDown(ImGuiMouseButton_Left);
}

static bool can_run_move_features(user_cmd* user_cmd) {
  if (user_cmd == nullptr || user_cmd->command_number == 0 || user_cmd->tick_count <= 0) {
    return false;
  }

  if (!engine->is_in_game()) {
    return false;
  }

  return entity_list->get_localplayer() != nullptr;
}

static bool aimbot_should_clear_autoreload() {
  if (!config.aimbot.master) {
    return false;
  }

  if (config.aimbot.key.button != SDLK_UNKNOWN && !is_button_active(config.aimbot.key)) {
    return false;
  }

  return aimbot::active_target_entity() != nullptr || aimbot::has_any_preference();
}

// really stupid but i guess it works :thumbsup: - pupnoodle
static void force_aimbot_autoreload_convar() {
  if (!config.aimbot.master || convar_system == nullptr) {
    return;
  }

  static Convar* cl_autoreload = nullptr;
  if (cl_autoreload == nullptr) {
    cl_autoreload = convar_system->find_var("cl_autoreload");
  }

  if (cl_autoreload != nullptr && cl_autoreload->get_int() != 0) {
    cl_autoreload->set_int(0);
  }
}

static bool should_run_taunt_slide(Player* localplayer) {
  if (!config.misc.movement.taunt_slide || localplayer == nullptr || !localplayer->is_alive()) {
    return false;
  }

  return localplayer->in_cond(TF_COND_TAUNTING) && localplayer->allow_move_during_taunt();
}

static bool run_move_features(user_cmd* user_cmd) {
  aimbot::clear_frame_target();
  backtrack::on_create_move(user_cmd);
  const Vec3 original_view_angles = user_cmd->view_angles;

  force_aimbot_autoreload_convar();

  medic_automation::controller().on_pre_navbot_create_move(user_cmd);
  bhop(user_cmd);
  navbot::controller().on_create_move(user_cmd);
  medic_automation::controller().on_post_navbot_create_move(user_cmd);
  const bool suppress_aimbot_for_reload = navbot::controller().should_suppress_aimbot();
  const bool suppress_aimbot_for_medic = medic_automation::controller().should_suppress_aimbot();
  const bool suppress_aimbot = suppress_aimbot_for_reload || suppress_aimbot_for_medic;

  const bool menu_movement_blocked = should_block_menu_movement();
  if (menu_movement_blocked) {
    user_cmd->sidemove = 0.0f;
    user_cmd->forwardmove = 0.0f;
  }

  const float corrected_side_move = user_cmd->sidemove;
  const float corrected_forward_move = user_cmd->forwardmove;

  if (suppress_aimbot_for_reload) {
    aimbot::clear_target_state();
    aimbot::reset_autoscope_state();
    aimbot::reset_input_history();
    user_cmd->buttons &= ~(IN_ATTACK | IN_ATTACK2 | IN_ATTACK3);
    user_cmd->buttons |= IN_RELOAD;
  } else if (suppress_aimbot_for_medic) {
    aimbot::clear_target_state();
    aimbot::reset_autoscope_state();
    aimbot::reset_input_history();
  } else if (aimbot_should_clear_autoreload()) {
    user_cmd->buttons &= ~IN_RELOAD;
  }

  start_engine_prediction(user_cmd);
  const aimbot::aimbot_run_result aimbot_result = suppress_aimbot
    ? aimbot::aimbot_run_result{}
    : aimbot::run(user_cmd, original_view_angles);
  movement_fix(user_cmd, original_view_angles, corrected_forward_move, corrected_side_move);
  if (!menu_movement_blocked && !suppress_aimbot && !navbot::controller().should_prioritize_danger_movement()) {
    aimbot::apply_walk_to_target(entity_list->get_localplayer(), user_cmd);
  }

  end_engine_prediction();

  const bool moonwalk_psilent = !menu_movement_blocked && moonwalk_create_move(user_cmd);

  const crit_hack::create_move_result crit_result = crit_hack::on_create_move(user_cmd);
  if (crit_result.attack_suppressed && aimbot_result.psilent_command) {
    user_cmd->view_angles = original_view_angles;
  }

  return (aimbot_result.psilent_command && !crit_result.attack_suppressed) || moonwalk_psilent;
}

// Called approx every frame.
// Only valid user commands are sent approx 66 times a second
bool client_mode_create_move_hook(void* me, float sample_time, user_cmd* user_cmd) {
  if (cathook::core::is_detach_pending()) {
    const bool rc = client_mode_create_move_original(me, sample_time, user_cmd);
    cathook::core::service_detach_request();
    return rc;
  }

  if (g_client_create_move_owns_features) {
    if (can_run_move_features(user_cmd)) {
      Player* localplayer = entity_list->get_localplayer();
      if (should_run_taunt_slide(localplayer)) {
        return false;
      }
    }

    return client_mode_create_move_original(me, sample_time, user_cmd);
  }

  cat_bind::run();
  automation::controller().on_create_move(user_cmd);

  const bool can_run_features = can_run_move_features(user_cmd);
  Player* localplayer = can_run_features ? entity_list->get_localplayer() : nullptr;
  if (should_run_taunt_slide(localplayer)) {
    return false;
  }

  const bool rc = client_mode_create_move_original(me, sample_time, user_cmd);

  if (!can_run_features) {
    return rc;
  }

  update_player_head_emoji_cache();

  const bool use_psilent = run_move_features(user_cmd);
  if (use_psilent) {
    return false;
  }

  return rc;
}
