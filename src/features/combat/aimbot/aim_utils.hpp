/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/combat/aimbot/aim_utils.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef AIM_UTILS_HPP
#define AIM_UTILS_HPP

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <climits>
#include <cstddef>
#include <cstring>

#include "aimbot_debug.hpp"
#include "aimbot.hpp"

#include "core/entity_cache.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/math/math.hpp"

#include "features/automation/nographics/nographics.hpp"
#include "features/combat/aimbot/projectile/projectile_live_data.hpp"
#include "features/menu/config.hpp"
#include "features/movement/local_prediction/move_sim.hpp"

#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/building.hpp"
#include "games/tf2/sdk/aim_hitboxes.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

struct aimbot_candidate {
  Entity* entity = nullptr;
  Player* player = nullptr;
  int bone = 0;
  int hitbox = -1;
  int studio_hitbox = -1;
  Vec3 aim_position{};
  Vec3 aim_angles{};
  float fov = FLT_MAX;
  float distance = FLT_MAX;
  int health = 0;
  float simulation_time = 0.0f;
  int tick_count = 0;
  Vec3 command_angles{};
  Vec3 backtrack_mins{};
  Vec3 backtrack_maxs{};
  Vec3 backtrack_hitbox_mins{};
  Vec3 backtrack_hitbox_maxs{};
  matrix_3x4 backtrack_bone{};
  bool spread_compensated = false;
  bool backtrack = false;
  bool backtrack_hitbox_valid = false;
  int pellet_index = -1;
  int pellet_count = 0;
  float spread = 0.0f;
  aimbot_debug_reason debug_reason = aimbot_debug_reason::none;
  aimbot_reject_debug reject_debug{};
  bool visible = false;
  bool preferred = false;
  bool projectile_direct = false;
  bool projectile_splash = false;
  bool projectile_has_target_base_origin = false;
  float projectile_intercept_time = 0.0f;
  float projectile_miss_distance = FLT_MAX;
  float projectile_splash_radius = 0.0f;
  Vec3 projectile_target_base_origin{};
  Vec3 projectile_target_offset{};
  bool melee_has_prediction = false;
  float melee_impact_time = 0.0f;
  Vec3 melee_swing_start{};
  Vec3 melee_target_origin{};
};

struct aimbot_point {
  bool valid = false;
  int bone = 0;
  int hitbox = -1;
  int studio_hitbox = -1;
  int priority = INT_MAX;
  Vec3 position{};
  Vec3 angles{};
  float fov = FLT_MAX;
};

inline static float aimbot_scoped_begin_time = 0.0f;
constexpr int aimbot_max_bones = 128;
constexpr int aimbot_bone_mask = 0x7FF00;

struct aimbot_bone_cache_bypass {
  bool previous = false;
  bool changed = false;

  aimbot_bone_cache_bypass()
  {
    previous = config.misc.exploits.setup_bones_optimization;
    changed = previous;
    if (changed) {
      config.misc.exploits.setup_bones_optimization = false;
    }
  }

  ~aimbot_bone_cache_bypass()
  {
    if (changed) {
      config.misc.exploits.setup_bones_optimization = previous;
    }
  }
};

inline bool aimbot_setup_bones(Player* target, matrix_3x4* bone_to_world) {
  if (target == nullptr || bone_to_world == nullptr) {
    return false;
  }

  aimbot_bone_cache_bypass bypass{};
  return target->setup_bones(
    bone_to_world,
    aimbot_max_bones,
    aimbot_bone_mask,
    target->get_simulation_time()) != 0;
}

inline bool aimbot_setup_studio_hitboxes(Player* target,
  studio_hitbox_set** hitbox_set_out,
  matrix_3x4* bone_to_world) {
  if (target == nullptr || hitbox_set_out == nullptr || bone_to_world == nullptr || model_info == nullptr) {
    return false;
  }

  const model_t* model = target->get_model();
  if (model == nullptr) {
    return false;
  }

  studio_hdr* hdr = model_info->get_studio_model(model);
  studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(target->get_hitbox_set()) : nullptr;
  if (hitbox_set == nullptr) {
    return false;
  }

  if (!aimbot_setup_bones(target, bone_to_world)) {
    return false;
  }

  *hitbox_set_out = hitbox_set;
  return true;
}

inline bool aimbot_vec3_is_finite(const Vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline bool aimbot_simple_simulation_enabled() {
  return nographics::is_enabled();
}

inline void reset_aimbot_scope_timing() {
  aimbot_scoped_begin_time = 0.0f;
}

inline void update_aimbot_scope_timing(Player* localplayer) {
  if (localplayer == nullptr || !localplayer->is_scoped()) {
    reset_aimbot_scope_timing();
    return;
  }

  if (aimbot_scoped_begin_time > 0.0f) {
    return;
  }

  aimbot_scoped_begin_time = global_vars != nullptr
    ? global_vars->curtime
    : localplayer->get_tickbase() * static_cast<float>(TICK_INTERVAL);
}

inline float aimbot_tracked_scoped_time(Player* localplayer) {
  if (localplayer == nullptr || !localplayer->is_scoped() || aimbot_scoped_begin_time <= 0.0f) {
    return 0.0f;
  }

  const float current_time = global_vars != nullptr
    ? global_vars->curtime
    : localplayer->get_tickbase() * static_cast<float>(TICK_INTERVAL);
  return std::max(current_time - aimbot_scoped_begin_time, 0.0f);
}

inline bool aimbot_sniper_scope_time_ready(Player* localplayer) {
  if (localplayer == nullptr || !localplayer->is_scoped() || localplayer->get_fov() >= localplayer->get_default_fov()) {
    return false;
  }

  constexpr float sniper_headshot_scope_delay = 0.2f;
  if (aimbot_tracked_scoped_time(localplayer) >= sniper_headshot_scope_delay) {
    return true;
  }

  const float current_time = global_vars != nullptr
    ? global_vars->curtime
    : localplayer->get_tickbase() * static_cast<float>(TICK_INTERVAL);
  return current_time - localplayer->get_fov_time() >= sniper_headshot_scope_delay;
}

inline float aimbot_distance_squared(const Vec3& left, const Vec3& right) {
  const Vec3 delta = left - right;
  return (delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z);
}

inline Vec3 aimbot_transform_point(const Vec3& point, const matrix_3x4& matrix) {
  return Vec3{
    (point.x * matrix.mat[0][0]) + (point.y * matrix.mat[0][1]) + (point.z * matrix.mat[0][2]) + matrix.mat[0][3],
    (point.x * matrix.mat[1][0]) + (point.y * matrix.mat[1][1]) + (point.z * matrix.mat[1][2]) + matrix.mat[1][3],
    (point.x * matrix.mat[2][0]) + (point.y * matrix.mat[2][1]) + (point.z * matrix.mat[2][2]) + matrix.mat[2][3]
  };
}

inline Vec3 aimbot_inverse_transform_point(const Vec3& point, const matrix_3x4& matrix) {
  const Vec3 delta{
    point.x - matrix.mat[0][3],
    point.y - matrix.mat[1][3],
    point.z - matrix.mat[2][3]
  };

  return Vec3{
    (delta.x * matrix.mat[0][0]) + (delta.y * matrix.mat[1][0]) + (delta.z * matrix.mat[2][0]),
    (delta.x * matrix.mat[0][1]) + (delta.y * matrix.mat[1][1]) + (delta.z * matrix.mat[2][1]),
    (delta.x * matrix.mat[0][2]) + (delta.y * matrix.mat[1][2]) + (delta.z * matrix.mat[2][2])
  };
}

inline Vec3 aimbot_clamp_to_hitbox(const Vec3& point, const studio_box& hitbox) {
  return Vec3{
    std::clamp(point.x, hitbox.bbmin.x, hitbox.bbmax.x),
    std::clamp(point.y, hitbox.bbmin.y, hitbox.bbmax.y),
    std::clamp(point.z, hitbox.bbmin.z, hitbox.bbmax.z)
  };
}

inline bool aimbot_add_local_hitbox_point(Vec3* points, int* point_count, int max_points, const Vec3& point) {
  if (points == nullptr || point_count == nullptr || *point_count >= max_points || !aimbot_vec3_is_finite(point)) {
    return false;
  }

  for (int index = 0; index < *point_count; ++index) {
    if (aimbot_distance_squared(points[index], point) < 0.25f) {
      return false;
    }
  }

  points[*point_count] = point;
  ++(*point_count);
  return true;
}

inline float aimbot_effective_multipoint_scale() {
  const float configured = config.aimbot.multipoint_scale;
  if (!std::isfinite(configured)) {
    return 0.0f;
  }

  return std::clamp(configured, 0.0f, 100.0f) / 100.0f;
}

inline float aimbot_effective_bone_size_subtract() {
  const float configured = config.aimbot.bone_size_subtract;
  if (!std::isfinite(configured)) {
    return 0.0f;
  }

  return std::clamp(configured, 0.0f, 12.0f);
}

inline float aimbot_effective_bone_size_min_scale() {
  const float configured = config.aimbot.bone_size_min_scale;
  if (!std::isfinite(configured)) {
    return 0.4f;
  }

  return std::clamp(configured, 0.05f, 1.0f);
}

inline int aimbot_build_local_hitbox_points(const studio_box& hitbox,
  const matrix_3x4& bone_to_world,
  const Vec3& shoot_pos,
  Vec3* points,
  int max_points,
  bool include_multipoint) {
  int point_count = 0;
  const Vec3 center = (hitbox.bbmin + hitbox.bbmax) * 0.5f;
  aimbot_add_local_hitbox_point(points, &point_count, max_points, center);

  if (!include_multipoint) {
    return point_count;
  }

  const float scale = std::max(aimbot_effective_multipoint_scale(), aimbot_effective_bone_size_min_scale());
  if (scale <= 0.0f) {
    return point_count;
  }

  const Vec3 local_shoot_pos = aimbot_inverse_transform_point(shoot_pos, bone_to_world);
  aimbot_add_local_hitbox_point(points, &point_count, max_points, aimbot_clamp_to_hitbox(local_shoot_pos, hitbox));

  const float subtract = aimbot_effective_bone_size_subtract();
  const Vec3 extent_raw = (hitbox.bbmax - hitbox.bbmin) * 0.5f;
  const Vec3 extent{
    std::max(extent_raw.x - subtract, extent_raw.x * aimbot_effective_bone_size_min_scale()),
    std::max(extent_raw.y - subtract, extent_raw.y * aimbot_effective_bone_size_min_scale()),
    std::max(extent_raw.z - subtract, extent_raw.z * aimbot_effective_bone_size_min_scale())
  };

  const Vec3 scaled_extent = extent * scale;
  if (std::fabs(scaled_extent.x) > 1.0f &&
      std::fabs(scaled_extent.y) > 1.0f &&
      std::fabs(scaled_extent.z) > 1.0f) {
    for (const float x_sign : { -1.0f, 1.0f }) {
      for (const float y_sign : { -1.0f, 1.0f }) {
        for (const float z_sign : { -1.0f, 1.0f }) {
          aimbot_add_local_hitbox_point(
            points,
            &point_count,
            max_points,
            center + Vec3{ scaled_extent.x * x_sign, scaled_extent.y * y_sign, scaled_extent.z * z_sign });
        }
      }
    }
  }

  return point_count;
}

inline unsigned int aimbot_visibility_trace_mask() {
  unsigned int trace_mask = MASK_SHOT | CONTENTS_GRATE;
  if (config.aimbot.shoot_through_glass) {
    trace_mask &= ~CONTENTS_WINDOW;
  }

  return trace_mask;
}

inline unsigned int aimbot_hitscan_trace_mask() {
  unsigned int trace_mask = MASK_SHOT | CONTENTS_GRATE;
  if (config.aimbot.shoot_through_glass) {
    trace_mask &= ~CONTENTS_WINDOW;
  }

  return trace_mask;
}

inline bool is_player_visible(Player* localplayer, Player* entity, int bone) {
  if (localplayer == nullptr || entity == nullptr || engine_trace == nullptr) return false;

  Vec3 start_pos = localplayer->get_shoot_pos();
  Vec3 target_pos = entity->get_bone_pos(bone);

  struct ray_t ray = engine_trace->init_ray(&start_pos, &target_pos);
  struct trace_filter filter;
  engine_trace->init_trace_filter(&filter, localplayer);

  struct trace_t trace_world{};
  engine_trace->trace_ray(&ray, aimbot_visibility_trace_mask(), &filter, &trace_world);
  return trace_world.entity == entity || (!trace_world.all_solid && !trace_world.start_solid && trace_world.fraction >= 0.999f);
}

inline bool aimbot_trace_visible_to_position(Player* localplayer,
  Entity* target,
  const Vec3& target_pos,
  unsigned int trace_mask = aimbot_visibility_trace_mask()) {
  if (localplayer == nullptr || target == nullptr || engine_trace == nullptr || !aimbot_vec3_is_finite(target_pos)) return false;

  Vec3 start_pos = localplayer->get_shoot_pos();
  Vec3 end_pos = target_pos;

  struct ray_t ray = engine_trace->init_ray(&start_pos, &end_pos);
  struct trace_filter filter;
  engine_trace->init_trace_filter(&filter, localplayer);

  struct trace_t trace_world{};
  engine_trace->trace_ray(&ray, trace_mask, &filter, &trace_world);
  return trace_world.entity == target || (!trace_world.all_solid && !trace_world.start_solid && trace_world.fraction >= 0.999f);
}

inline Vec3 aimbot_calculate_angles_to_position(const Vec3& start, const Vec3& target) {
  Vec3 diff{
    target.x - start.x,
    target.y - start.y,
    target.z - start.z
  };
  float yaw_hyp_sq = (diff.x * diff.x) + (diff.y * diff.y);
  if (yaw_hyp_sq <= 1e-12f) {
    return Vec3{
      diff.z > 0.0f ? -89.0f : (diff.z < 0.0f ? 89.0f : 0.0f),
      0.0f,
      0.0f
    };
  }
  float yaw_hyp = std::sqrt(yaw_hyp_sq);
  return Vec3{
    -std::atan2(diff.z, yaw_hyp) * radpi,
    std::atan2(diff.y, diff.x) * radpi,
    0.0f
  };
}

inline Vec3 aimbot_normalize_angle_delta(const Vec3& target_angles, const Vec3& source_angles) {
  float x_diff = target_angles.x - source_angles.x;
  float y_diff = target_angles.y - source_angles.y;

  return Vec3{
    std::clamp(std::remainder(x_diff, 360.0f), -89.0f, 89.0f),
    std::clamp(std::remainder(y_diff, 360.0f), -180.0f, 180.0f),
    0.0f
  };
}

inline Vec3 aimbot_clamp_angles(Vec3 angles) {
  angles.x = std::clamp(angles.x, -89.0f, 89.0f);
  angles.y = std::remainder(angles.y, 360.0f);
  angles.z = 0.0f;
  return angles;
}

inline float aimbot_calculate_fov(const Vec3& target_angles, const Vec3& source_angles) {
  Vec3 delta = aimbot_normalize_angle_delta(target_angles, source_angles);
  return std::hypot(delta.x, delta.y);
}

inline bool aimbot_headshot_ready_for_priority(Player* localplayer, Weapon* weapon) {
  if (localplayer == nullptr || weapon == nullptr || !weapon->is_headshot_weapon()) {
    return false;
  }

  if (weapon->can_fire_critical_shot(true)) {
    return true;
  }

  if (weapon->is_sniper_rifle()) {
    if (attribute_manager != nullptr && attribute_manager->attrib_hook_value(0, "sniper_no_headshot_without_full_charge", weapon->to_entity()) != 0) {
      return weapon->get_charged_damage() >= 150.0f;
    }

    if (attribute_manager != nullptr && attribute_manager->attrib_hook_value(0, "sniper_crit_no_scope", weapon->to_entity()) != 0) {
      return true;
    }

    return aimbot_sniper_scope_time_ready(localplayer);
  }

  switch (weapon->get_weapon_id()) {
  case TF_WEAPON_REVOLVER:
    return attribute_manager == nullptr ||
      attribute_manager->attrib_hook_value(0, "set_weapon_mode", weapon->to_entity()) != 1 ||
      weapon->can_ambassador_headshot();
  default:
    return false;
  }
}

inline int aimbot_default_bone(Player* localplayer, Player* target, Weapon* weapon) {
  if (localplayer == nullptr || target == nullptr) return 0;

  int bone = target->get_tf_class() == tf_class::ENGINEER ? 5 : 2;
  if (aimbot_headshot_ready_for_priority(localplayer, weapon)) {
    bone = target->get_head_bone();
  }

  return bone;
}

inline bool aimbot_model_name_is(Player* target, const char* name) {
  if (target == nullptr || name == nullptr || model_info == nullptr) {
    return false;
  }

  const model_t* model = target->get_model();
  studio_hdr* hdr = model != nullptr ? model_info->get_studio_model(model) : nullptr;
  if (hdr == nullptr) {
    return false;
  }

  const std::size_t name_length = std::strlen(name);
  if (name_length >= sizeof(hdr->name)) {
    return false;
  }

  return std::strncmp(hdr->name, name, name_length) == 0 && hdr->name[name_length] == '\0';
}

inline bool aimbot_model_name_is_any(Player* target, const char* first, const char* second, const char* third = nullptr) {
  return aimbot_model_name_is(target, first) ||
    aimbot_model_name_is(target, second) ||
    (third != nullptr && aimbot_model_name_is(target, third));
}

inline int aimbot_studio_hitbox_to_base(Player* target, int hitbox_id) {
  if (aimbot_model_name_is(target, "models/bots/engineer/bot_engineer.mdl")) {
    switch (hitbox_id) {
    case 0: return aim_hitbox_head;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 5: return aim_hitbox_left_upper_arm;
    case 6: return aim_hitbox_left_forearm;
    case 7: return aim_hitbox_left_hand;
    case 8: return aim_hitbox_right_upper_arm;
    case 9: return aim_hitbox_right_forearm;
    case 10: return aim_hitbox_right_hand;
    case 11: return aim_hitbox_left_thigh;
    case 12: return aim_hitbox_left_calf;
    case 13: return aim_hitbox_left_foot;
    case 14: return aim_hitbox_right_thigh;
    case 15: return aim_hitbox_right_calf;
    case 16: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is_any(target, "models/vsh/player/saxton_hale.mdl", "models/vsh/player/hell_hale.mdl", "models/vsh/player/santa_hale.mdl")) {
    switch (hitbox_id) {
    case 0: return aim_hitbox_head;
    case 1:
    case 14: return aim_hitbox_pelvis;
    case 15: return aim_hitbox_spine_0;
    case 16: return aim_hitbox_spine_1;
    case 17: return aim_hitbox_spine_2;
    case 18: return aim_hitbox_spine_3;
    case 12: return aim_hitbox_left_upper_arm;
    case 10: return aim_hitbox_left_forearm;
    case 8: return aim_hitbox_left_hand;
    case 13: return aim_hitbox_right_upper_arm;
    case 11: return aim_hitbox_right_forearm;
    case 9: return aim_hitbox_right_hand;
    case 6: return aim_hitbox_left_thigh;
    case 4: return aim_hitbox_left_calf;
    case 2: return aim_hitbox_left_foot;
    case 7: return aim_hitbox_right_thigh;
    case 5: return aim_hitbox_right_calf;
    case 3: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is_any(target, "models/player/scout_infected.mdl", "models/player/soldier_infected.mdl", "models/player/sniper_infected.mdl")) {
    switch (hitbox_id) {
    case 6: return aim_hitbox_head;
    case 0:
    case 5: return aim_hitbox_pelvis;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 7:
    case 9: return aim_hitbox_left_upper_arm;
    case 11: return aim_hitbox_left_forearm;
    case 19: return aim_hitbox_left_hand;
    case 8:
    case 10: return aim_hitbox_right_upper_arm;
    case 12: return aim_hitbox_right_forearm;
    case 20: return aim_hitbox_right_hand;
    case 13: return aim_hitbox_left_thigh;
    case 15: return aim_hitbox_left_calf;
    case 17: return aim_hitbox_left_foot;
    case 14: return aim_hitbox_right_thigh;
    case 16: return aim_hitbox_right_calf;
    case 18: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/pyro_infected.mdl")) {
    switch (hitbox_id) {
    case 6: return aim_hitbox_head;
    case 0:
    case 5: return aim_hitbox_pelvis;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 7:
    case 8: return aim_hitbox_left_upper_arm;
    case 9: return aim_hitbox_left_forearm;
    case 10: return aim_hitbox_left_hand;
    case 11:
    case 12: return aim_hitbox_right_upper_arm;
    case 13: return aim_hitbox_right_forearm;
    case 14: return aim_hitbox_right_hand;
    case 15: return aim_hitbox_left_thigh;
    case 16: return aim_hitbox_left_calf;
    case 17: return aim_hitbox_left_foot;
    case 19: return aim_hitbox_right_thigh;
    case 20: return aim_hitbox_right_calf;
    case 21: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/demo_infected.mdl")) {
    switch (hitbox_id) {
    case 16: return aim_hitbox_head;
    case 0:
    case 15: return aim_hitbox_pelvis;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 5:
    case 6: return aim_hitbox_left_upper_arm;
    case 13: return aim_hitbox_left_forearm;
    case 17: return aim_hitbox_left_hand;
    case 7:
    case 8: return aim_hitbox_right_upper_arm;
    case 14: return aim_hitbox_right_forearm;
    case 18: return aim_hitbox_right_hand;
    case 9: return aim_hitbox_left_thigh;
    case 10: return aim_hitbox_left_calf;
    case 19: return aim_hitbox_left_foot;
    case 11: return aim_hitbox_right_thigh;
    case 12: return aim_hitbox_right_calf;
    case 20: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/heavy_infected.mdl")) {
    switch (hitbox_id) {
    case 6: return aim_hitbox_head;
    case 0:
    case 5: return aim_hitbox_pelvis;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 7:
    case 9: return aim_hitbox_left_upper_arm;
    case 11: return aim_hitbox_left_forearm;
    case 17: return aim_hitbox_left_hand;
    case 8:
    case 10: return aim_hitbox_right_upper_arm;
    case 12: return aim_hitbox_right_forearm;
    case 18: return aim_hitbox_right_hand;
    case 13: return aim_hitbox_left_thigh;
    case 15: return aim_hitbox_left_calf;
    case 19: return aim_hitbox_left_foot;
    case 14: return aim_hitbox_right_thigh;
    case 16: return aim_hitbox_right_calf;
    case 20: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/engineer_infected.mdl")) {
    switch (hitbox_id) {
    case 8: return aim_hitbox_head;
    case 0:
    case 7: return aim_hitbox_pelvis;
    case 3: return aim_hitbox_spine_0;
    case 4: return aim_hitbox_spine_1;
    case 5: return aim_hitbox_spine_2;
    case 6: return aim_hitbox_spine_3;
    case 11:
    case 12: return aim_hitbox_left_upper_arm;
    case 13: return aim_hitbox_left_forearm;
    case 20: return aim_hitbox_left_hand;
    case 14:
    case 15: return aim_hitbox_right_upper_arm;
    case 16: return aim_hitbox_right_forearm;
    case 19: return aim_hitbox_right_hand;
    case 9: return aim_hitbox_left_thigh;
    case 10: return aim_hitbox_left_calf;
    case 17: return aim_hitbox_left_foot;
    case 1: return aim_hitbox_right_thigh;
    case 2: return aim_hitbox_right_calf;
    case 18: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/medic_infected.mdl")) {
    switch (hitbox_id) {
    case 6: return aim_hitbox_head;
    case 0:
    case 5: return aim_hitbox_pelvis;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 7:
    case 9: return aim_hitbox_left_upper_arm;
    case 11: return aim_hitbox_left_forearm;
    case 17: return aim_hitbox_left_hand;
    case 8:
    case 10: return aim_hitbox_right_upper_arm;
    case 12: return aim_hitbox_right_forearm;
    case 18: return aim_hitbox_right_hand;
    case 13: return aim_hitbox_left_thigh;
    case 15: return aim_hitbox_left_calf;
    case 22: return aim_hitbox_left_foot;
    case 14: return aim_hitbox_right_thigh;
    case 16: return aim_hitbox_right_calf;
    case 23: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/spy_infected.mdl")) {
    switch (hitbox_id) {
    case 6: return aim_hitbox_head;
    case 0:
    case 5: return aim_hitbox_pelvis;
    case 1: return aim_hitbox_spine_0;
    case 2: return aim_hitbox_spine_1;
    case 3: return aim_hitbox_spine_2;
    case 4: return aim_hitbox_spine_3;
    case 7:
    case 9: return aim_hitbox_left_upper_arm;
    case 11: return aim_hitbox_left_forearm;
    case 13: return aim_hitbox_left_hand;
    case 8:
    case 10: return aim_hitbox_right_upper_arm;
    case 12: return aim_hitbox_right_forearm;
    case 14: return aim_hitbox_right_hand;
    case 15: return aim_hitbox_left_thigh;
    case 17: return aim_hitbox_left_calf;
    case 19: return aim_hitbox_left_foot;
    case 16: return aim_hitbox_right_thigh;
    case 18: return aim_hitbox_right_calf;
    case 20: return aim_hitbox_right_foot;
    default: return -1;
    }
  }

  return hitbox_id;
}

inline int aimbot_base_hitbox_to_studio(Player* target, int hitbox_id) {
  if (aimbot_model_name_is(target, "models/bots/engineer/bot_engineer.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 0;
    case aim_hitbox_pelvis:
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 5;
    case aim_hitbox_left_forearm: return 6;
    case aim_hitbox_left_hand: return 7;
    case aim_hitbox_right_upper_arm: return 8;
    case aim_hitbox_right_forearm: return 9;
    case aim_hitbox_right_hand: return 10;
    case aim_hitbox_left_thigh: return 11;
    case aim_hitbox_left_calf: return 12;
    case aim_hitbox_left_foot: return 13;
    case aim_hitbox_right_thigh: return 14;
    case aim_hitbox_right_calf: return 15;
    case aim_hitbox_right_foot: return 16;
    default: return -1;
    }
  }

  if (aimbot_model_name_is_any(target, "models/vsh/player/saxton_hale.mdl", "models/vsh/player/hell_hale.mdl", "models/vsh/player/santa_hale.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 0;
    case aim_hitbox_pelvis: return 14;
    case aim_hitbox_spine_0: return 15;
    case aim_hitbox_spine_1: return 16;
    case aim_hitbox_spine_2: return 17;
    case aim_hitbox_spine_3: return 18;
    case aim_hitbox_left_upper_arm: return 12;
    case aim_hitbox_left_forearm: return 10;
    case aim_hitbox_left_hand: return 8;
    case aim_hitbox_right_upper_arm: return 13;
    case aim_hitbox_right_forearm: return 11;
    case aim_hitbox_right_hand: return 9;
    case aim_hitbox_left_thigh: return 6;
    case aim_hitbox_left_calf: return 4;
    case aim_hitbox_left_foot: return 2;
    case aim_hitbox_right_thigh: return 7;
    case aim_hitbox_right_calf: return 5;
    case aim_hitbox_right_foot: return 3;
    default: return -1;
    }
  }

  if (aimbot_model_name_is_any(target, "models/player/scout_infected.mdl", "models/player/soldier_infected.mdl", "models/player/sniper_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 6;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 9;
    case aim_hitbox_left_forearm: return 11;
    case aim_hitbox_left_hand: return 19;
    case aim_hitbox_right_upper_arm: return 10;
    case aim_hitbox_right_forearm: return 12;
    case aim_hitbox_right_hand: return 20;
    case aim_hitbox_left_thigh: return 13;
    case aim_hitbox_left_calf: return 15;
    case aim_hitbox_left_foot: return 17;
    case aim_hitbox_right_thigh: return 14;
    case aim_hitbox_right_calf: return 16;
    case aim_hitbox_right_foot: return 18;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/pyro_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 6;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 8;
    case aim_hitbox_left_forearm: return 9;
    case aim_hitbox_left_hand: return 10;
    case aim_hitbox_right_upper_arm: return 12;
    case aim_hitbox_right_forearm: return 13;
    case aim_hitbox_right_hand: return 14;
    case aim_hitbox_left_thigh: return 15;
    case aim_hitbox_left_calf: return 16;
    case aim_hitbox_left_foot: return 17;
    case aim_hitbox_right_thigh: return 19;
    case aim_hitbox_right_calf: return 20;
    case aim_hitbox_right_foot: return 21;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/demo_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 16;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 6;
    case aim_hitbox_left_forearm: return 13;
    case aim_hitbox_left_hand: return 17;
    case aim_hitbox_right_upper_arm: return 8;
    case aim_hitbox_right_forearm: return 14;
    case aim_hitbox_right_hand: return 18;
    case aim_hitbox_left_thigh: return 9;
    case aim_hitbox_left_calf: return 10;
    case aim_hitbox_left_foot: return 19;
    case aim_hitbox_right_thigh: return 11;
    case aim_hitbox_right_calf: return 12;
    case aim_hitbox_right_foot: return 20;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/heavy_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 6;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 9;
    case aim_hitbox_left_forearm: return 11;
    case aim_hitbox_left_hand: return 17;
    case aim_hitbox_right_upper_arm: return 10;
    case aim_hitbox_right_forearm: return 12;
    case aim_hitbox_right_hand: return 18;
    case aim_hitbox_left_thigh: return 13;
    case aim_hitbox_left_calf: return 15;
    case aim_hitbox_left_foot: return 19;
    case aim_hitbox_right_thigh: return 14;
    case aim_hitbox_right_calf: return 16;
    case aim_hitbox_right_foot: return 20;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/engineer_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 8;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 3;
    case aim_hitbox_spine_1: return 4;
    case aim_hitbox_spine_2: return 5;
    case aim_hitbox_spine_3: return 6;
    case aim_hitbox_left_upper_arm: return 12;
    case aim_hitbox_left_forearm: return 13;
    case aim_hitbox_left_hand: return 20;
    case aim_hitbox_right_upper_arm: return 15;
    case aim_hitbox_right_forearm: return 16;
    case aim_hitbox_right_hand: return 19;
    case aim_hitbox_left_thigh: return 9;
    case aim_hitbox_left_calf: return 10;
    case aim_hitbox_left_foot: return 17;
    case aim_hitbox_right_thigh: return 1;
    case aim_hitbox_right_calf: return 2;
    case aim_hitbox_right_foot: return 18;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/medic_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 6;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 9;
    case aim_hitbox_left_forearm: return 11;
    case aim_hitbox_left_hand: return 17;
    case aim_hitbox_right_upper_arm: return 10;
    case aim_hitbox_right_forearm: return 12;
    case aim_hitbox_right_hand: return 18;
    case aim_hitbox_left_thigh: return 13;
    case aim_hitbox_left_calf: return 15;
    case aim_hitbox_left_foot: return 22;
    case aim_hitbox_right_thigh: return 14;
    case aim_hitbox_right_calf: return 16;
    case aim_hitbox_right_foot: return 23;
    default: return -1;
    }
  }

  if (aimbot_model_name_is(target, "models/player/spy_infected.mdl")) {
    switch (hitbox_id) {
    case aim_hitbox_head: return 6;
    case aim_hitbox_pelvis: return 0;
    case aim_hitbox_spine_0: return 1;
    case aim_hitbox_spine_1: return 2;
    case aim_hitbox_spine_2: return 3;
    case aim_hitbox_spine_3: return 4;
    case aim_hitbox_left_upper_arm: return 9;
    case aim_hitbox_left_forearm: return 11;
    case aim_hitbox_left_hand: return 13;
    case aim_hitbox_right_upper_arm: return 10;
    case aim_hitbox_right_forearm: return 12;
    case aim_hitbox_right_hand: return 14;
    case aim_hitbox_left_thigh: return 15;
    case aim_hitbox_left_calf: return 17;
    case aim_hitbox_left_foot: return 19;
    case aim_hitbox_right_thigh: return 16;
    case aim_hitbox_right_calf: return 18;
    case aim_hitbox_right_foot: return 20;
    default: return -1;
    }
  }

  return hitbox_id;
}

inline bool aimbot_hitbox_matches_mask(int hitbox_id, uint32_t hitbox_mask) {
  switch (hitbox_id) {
  case aim_hitbox_head:
    return (hitbox_mask & aim_hitbox_mask_head) != 0;
  case aim_hitbox_pelvis:
    return (hitbox_mask & aim_hitbox_mask_pelvis) != 0;
  case aim_hitbox_spine_0:
  case aim_hitbox_spine_1:
  case aim_hitbox_spine_2:
  case aim_hitbox_spine_3:
    return (hitbox_mask & aim_hitbox_mask_body) != 0;
  case aim_hitbox_left_upper_arm:
  case aim_hitbox_left_forearm:
  case aim_hitbox_left_hand:
  case aim_hitbox_right_upper_arm:
  case aim_hitbox_right_forearm:
  case aim_hitbox_right_hand:
    return (hitbox_mask & aim_hitbox_mask_arms) != 0;
  case aim_hitbox_left_thigh:
  case aim_hitbox_left_calf:
  case aim_hitbox_left_foot:
  case aim_hitbox_right_thigh:
  case aim_hitbox_right_calf:
  case aim_hitbox_right_foot:
    return (hitbox_mask & aim_hitbox_mask_legs) != 0;
  default:
    return false;
  }
}

inline int aimbot_hitbox_priority(Player* localplayer, Player* target, Weapon* weapon, int hitbox_id) {
  bool prefer_head = false;
  if (localplayer != nullptr && target != nullptr) {
    const uint32_t modifiers = config.aimbot.hitscan_modifiers;
    const bool user_wants_head =
      (modifiers & Aim::hitscan_mod_wait_for_headshot) != 0;
    prefer_head = user_wants_head || aimbot_headshot_ready_for_priority(localplayer, weapon);
  }

  switch (hitbox_id) {
  case aim_hitbox_head:
    return prefer_head ? 0 : 1;
  case aim_hitbox_spine_0:
  case aim_hitbox_spine_1:
  case aim_hitbox_spine_2:
  case aim_hitbox_spine_3:
    return prefer_head ? 1 : 0;
  case aim_hitbox_pelvis:
    return 2;
  case aim_hitbox_left_upper_arm:
  case aim_hitbox_left_forearm:
  case aim_hitbox_left_hand:
  case aim_hitbox_right_upper_arm:
  case aim_hitbox_right_forearm:
  case aim_hitbox_right_hand:
    return 3;
  case aim_hitbox_left_thigh:
  case aim_hitbox_left_calf:
  case aim_hitbox_left_foot:
  case aim_hitbox_right_thigh:
  case aim_hitbox_right_calf:
  case aim_hitbox_right_foot:
    return 4;
  default:
    return INT_MAX;
  }
}

inline int aimbot_fallback_hitbox_for_mask(Player* localplayer, Player* target, Weapon* weapon, uint32_t hitbox_mask) {
  constexpr int fallback_hitboxes[] = {
    aim_hitbox_head,
    aim_hitbox_spine_3,
    aim_hitbox_spine_2,
    aim_hitbox_spine_1,
    aim_hitbox_spine_0,
    aim_hitbox_pelvis,
    aim_hitbox_left_upper_arm,
    aim_hitbox_right_upper_arm,
    aim_hitbox_left_thigh,
    aim_hitbox_right_thigh
  };

  int best_hitbox = -1;
  int best_priority = INT_MAX;
  for (const int hitbox_id : fallback_hitboxes) {
    if (!aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask)) {
      continue;
    }

    const int priority = aimbot_hitbox_priority(localplayer, target, weapon, hitbox_id);
    if (priority < best_priority) {
      best_hitbox = hitbox_id;
      best_priority = priority;
    }
  }

  return best_hitbox;
}

inline aimbot_point aimbot_find_best_point(Player* localplayer,
  Player* target,
  Weapon* weapon,
  const Vec3& original_view_angles,
  uint32_t hitbox_mask,
  bool require_visibility = true,
  unsigned int trace_mask = aimbot_visibility_trace_mask()) {
  aimbot_point best_point{};
  if (localplayer == nullptr || target == nullptr) {
    return best_point;
  }

  if (hitbox_mask == aim_hitbox_mask_none) {
    hitbox_mask = aim_hitbox_mask_default_hitscan;
  }

  studio_hitbox_set* hitbox_set = nullptr;
  matrix_3x4 bone_to_world[aimbot_max_bones]{};
  if (aimbot_setup_studio_hitboxes(target, &hitbox_set, bone_to_world)) {
    const Vec3 shoot_pos = localplayer->get_shoot_pos();
    for (int studio_hitbox_id = 0; studio_hitbox_id < hitbox_set->num_hitboxes; ++studio_hitbox_id) {
      studio_box* hitbox = hitbox_set->hitbox(studio_hitbox_id);
      if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= aimbot_max_bones) {
        continue;
      }

      const int hitbox_id = aimbot_studio_hitbox_to_base(target, studio_hitbox_id);
      if (hitbox_id < 0 || !aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask)) {
        continue;
      }

      constexpr int max_local_points = 10;
      Vec3 local_points[max_local_points]{};
      const int point_count = aimbot_build_local_hitbox_points(
        *hitbox,
        bone_to_world[hitbox->bone],
        shoot_pos,
        local_points,
        max_local_points,
        require_visibility);

      for (int point_index = 0; point_index < point_count; ++point_index) {
        const Vec3 hitbox_position = aimbot_transform_point(local_points[point_index], bone_to_world[hitbox->bone]);
        if (!aimbot_vec3_is_finite(hitbox_position)) {
          continue;
        }

        if (require_visibility && !aimbot_trace_visible_to_position(localplayer, target, hitbox_position, trace_mask)) {
          continue;
        }

        aimbot_point point{};
        point.valid = true;
        point.bone = hitbox->bone;
        point.hitbox = hitbox_id;
        point.studio_hitbox = studio_hitbox_id;
        point.priority = aimbot_hitbox_priority(localplayer, target, weapon, hitbox_id);
        point.position = hitbox_position;
        point.angles = aimbot_calculate_angles_to_position(shoot_pos, hitbox_position);
        point.fov = aimbot_calculate_fov(point.angles, original_view_angles);

        if (!best_point.valid ||
            point.priority < best_point.priority ||
            (point.priority == best_point.priority && point.fov < best_point.fov)) {
          best_point = point;
        }
      }
    }
  }

  if (best_point.valid) {
    return best_point;
  }

  for (int hitbox_id = aim_hitbox_head; hitbox_id <= aim_hitbox_right_foot; ++hitbox_id) {
    if (!aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask)) {
      continue;
    }

    Vec3 hitbox_position{};
    int hitbox_bone = 0;
    const int studio_hitbox_id = aimbot_base_hitbox_to_studio(target, hitbox_id);
    if (studio_hitbox_id < 0 ||
        !target->get_hitbox_center(studio_hitbox_id, &hitbox_position, &hitbox_bone)) {
      continue;
    }

    if (!aimbot_vec3_is_finite(hitbox_position)) {
      continue;
    }

    if (require_visibility && !aimbot_trace_visible_to_position(localplayer, target, hitbox_position, trace_mask)) {
      continue;
    }

    aimbot_point point{};
    point.valid = true;
    point.bone = hitbox_bone;
    point.hitbox = hitbox_id;
    point.studio_hitbox = studio_hitbox_id;
    point.priority = aimbot_hitbox_priority(localplayer, target, weapon, hitbox_id);
    point.position = hitbox_position;
    point.angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), hitbox_position);
    point.fov = aimbot_calculate_fov(point.angles, original_view_angles);

    if (!best_point.valid ||
        point.priority < best_point.priority ||
        (point.priority == best_point.priority && point.fov < best_point.fov)) {
      best_point = point;
    }
  }

  if (best_point.valid) {
    return best_point;
  }

  const int fallback_hitbox = aimbot_fallback_hitbox_for_mask(localplayer, target, weapon, hitbox_mask);
  if (fallback_hitbox < 0) {
    return {};
  }

  int fallback_bone = 0;
  Vec3 fallback_position{};
  const int fallback_studio_hitbox = aimbot_base_hitbox_to_studio(target, fallback_hitbox);
  if (fallback_studio_hitbox < 0 ||
      !target->get_hitbox_center(fallback_studio_hitbox, &fallback_position, &fallback_bone) ||
      !aimbot_vec3_is_finite(fallback_position)) {
    fallback_bone = fallback_hitbox == aim_hitbox_head
      ? target->get_head_bone()
      : aimbot_default_bone(localplayer, target, weapon);
    fallback_position = target->get_bone_pos(fallback_bone);
  }
  if (!aimbot_vec3_is_finite(fallback_position)) {
    return {};
  }

  if (require_visibility && !aimbot_trace_visible_to_position(localplayer, target, fallback_position, trace_mask)) {
    return {};
  }

  best_point.valid = true;
  best_point.bone = fallback_bone;
  best_point.hitbox = fallback_hitbox;
  best_point.studio_hitbox = fallback_studio_hitbox;
  best_point.priority = aimbot_hitbox_priority(localplayer, target, weapon, fallback_hitbox);
  best_point.position = fallback_position;
  best_point.angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), fallback_position);
  best_point.fov = aimbot_calculate_fov(best_point.angles, original_view_angles);
  return best_point;
}

inline bool aimbot_segment_aabb_enter_fraction(const Vec3& start,
  const Vec3& end,
  const Vec3& mins,
  const Vec3& maxs,
  float* enter_fraction_out = nullptr) {
  Vec3 delta = end - start;
  float enter = 0.0f;
  float exit = 1.0f;

  const auto clip_axis = [&](float start_axis, float delta_axis, float min_axis, float max_axis) -> bool {
    if (std::fabs(delta_axis) <= 0.0001f) {
      return start_axis >= min_axis && start_axis <= max_axis;
    }

    float inv_delta = 1.0f / delta_axis;
    float t1 = (min_axis - start_axis) * inv_delta;
    float t2 = (max_axis - start_axis) * inv_delta;
    if (t1 > t2) {
      std::swap(t1, t2);
    }

    enter = std::max(enter, t1);
    exit = std::min(exit, t2);
    return enter <= exit;
  };

  const bool intersects =
    clip_axis(start.x, delta.x, mins.x, maxs.x) &&
    clip_axis(start.y, delta.y, mins.y, maxs.y) &&
    clip_axis(start.z, delta.z, mins.z, maxs.z);
  if (intersects && enter_fraction_out != nullptr) {
    *enter_fraction_out = std::clamp(enter, 0.0f, 1.0f);
  }

  return intersects;
}

inline bool aimbot_segment_intersects_aabb(const Vec3& start,
  const Vec3& end,
  const Vec3& mins,
  const Vec3& maxs) {
  return aimbot_segment_aabb_enter_fraction(start, end, mins, maxs);
}

inline bool aimbot_is_repair_wrench(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
  case Engi_t_Wrench:
  case Engi_t_WrenchR:
  case Engi_t_TheGunslinger:
  case Engi_t_TheSouthernHospitality:
  case Engi_t_GoldenWrench:
  case Engi_t_TheJag:
  case Engi_t_TheEurekaEffect:
  case Engi_t_FestiveWrench:
  case Engi_t_SilverBotkillerWrenchMkI:
  case Engi_t_GoldBotkillerWrenchMkI:
  case Engi_t_RustBotkillerWrenchMkI:
  case Engi_t_BloodBotkillerWrenchMkI:
  case Engi_t_CarbonadoBotkillerWrenchMkI:
  case Engi_t_DiamondBotkillerWrenchMkI:
  case Engi_t_SilverBotkillerWrenchMkII:
  case Engi_t_GoldBotkillerWrenchMkII:
    return true;
  default:
    return false;
  }
}

inline bool aimbot_is_sword_melee(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
  case Demoman_t_TheEyelander:
  case Demoman_t_TheScotsmansSkullcutter:
  case Demoman_t_HorselessHeadlessHorsemannsHeadtaker:
  case Demoman_t_TheClaidheamhMor:
  case Demoman_t_TheHalfZatoichi:
  case Demoman_t_ThePersianPersuader:
  case Demoman_t_NessiesNineIron:
  case Demoman_t_FestiveEyelander:
    return true;
  default:
    return false;
  }
}

inline float aimbot_get_base_melee_range(Player* localplayer, Weapon* weapon) {
  if (localplayer != nullptr && localplayer->in_cond(TF_COND_SHIELD_CHARGE)) {
    return 128.0f;
  }

  return aimbot_is_sword_melee(weapon) ? 72.0f : 48.0f;
}

inline float aimbot_get_melee_range(Player* localplayer, Weapon* weapon, Player* target) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  float melee_range = aimbot_get_base_melee_range(localplayer, weapon);

  if (localplayer != nullptr && localplayer->get_model_scale() > 1.0f) {
    melee_range *= localplayer->get_model_scale();
  }

  if (attribute_manager != nullptr) {
    melee_range = attribute_manager->attrib_hook_value(melee_range, "melee_range_multiplier", weapon->to_entity());
  }

  if (target != nullptr &&
      localplayer != nullptr &&
      target->get_team() == localplayer->get_team() &&
      aimbot_is_repair_wrench(weapon)) {
    melee_range = 70.0f;
  }

  return std::isfinite(melee_range) ? std::max(melee_range, 0.0f) : 0.0f;
}

inline float aimbot_get_melee_hull(Player* localplayer, Weapon* weapon, Player* target) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  float melee_hull = 18.0f;
  if (attribute_manager != nullptr) {
    melee_hull = attribute_manager->attrib_hook_value(melee_hull, "melee_bounds_multiplier", weapon->to_entity());
  }

  if (localplayer != nullptr && localplayer->get_model_scale() > 1.0f) {
    melee_hull *= localplayer->get_model_scale();
  }

  if (target != nullptr &&
      localplayer != nullptr &&
      target->get_team() == localplayer->get_team() &&
      aimbot_is_repair_wrench(weapon)) {
    melee_hull = 18.0f;
  }

  return std::isfinite(melee_hull) ? std::max(melee_hull, 0.0f) : 0.0f;
}

inline bool aimbot_melee_trace_clear_to_entity(Player* localplayer,
  Entity* target,
  const Vec3& start,
  const Vec3& end,
  const Vec3& hull_mins,
  const Vec3& hull_maxs,
  float target_enter_fraction,
  bool* hit_target_out = nullptr) {
  if (localplayer == nullptr || target == nullptr || engine_trace == nullptr) {
    return false;
  }

  if (hit_target_out != nullptr) {
    *hit_target_out = false;
  }

  Vec3 trace_start = start;
  Vec3 trace_end = end;
  Vec3 trace_mins = hull_mins;
  Vec3 trace_maxs = hull_maxs;
  ray_t ray = engine_trace->init_ray(&trace_start, &trace_end, &trace_mins, &trace_maxs);
  trace_filter filter{};
  engine_trace->init_trace_filter(&filter, localplayer);

  trace_t trace{};
  engine_trace->trace_ray(&ray, MASK_SOLID, &filter, &trace);
  if (trace.all_solid || trace.start_solid) {
    return false;
  }

  if (trace.entity == target) {
    if (hit_target_out != nullptr) {
      *hit_target_out = true;
    }
    return true;
  }

  return trace.fraction >= 1.0f || trace.fraction + 0.001f >= target_enter_fraction;
}

inline bool aimbot_is_friendlyfire_enabled() {
  static Convar* friendlyfire = convar_system->find_var("mp_friendlyfire");
  return friendlyfire != nullptr && friendlyfire->get_int() != 0;
}

inline bool aimbot_aim_at_enabled(uint32_t flag) {
  return (config.aimbot.aim_at & flag) != 0;
}

inline bool aimbot_has_mvm_bot_model(Player* player) {
  if (player == nullptr) {
    return false;
  }

  const char* model_name = player->get_model_name();
  return strstr(model_name, "models/bots/") != nullptr;
}

inline bool aimbot_is_fake_player(Player* player) {
  if (player == nullptr || engine == nullptr) {
    return false;
  }

  player_info info{};
  return engine->get_player_info(player->get_index(), &info) && info.fakeplayer;
}

inline bool aimbot_should_target_player_type(Player* player) {
  if (aimbot_has_mvm_bot_model(player)) {
    return aimbot_aim_at_enabled(Aim::aim_at_mvm_robots);
  }

  if (aimbot_is_fake_player(player)) {
    return aimbot_aim_at_enabled(Aim::aim_at_enemies) ||
      aimbot_aim_at_enabled(Aim::aim_at_mvm_robots);
  }

  return aimbot_aim_at_enabled(Aim::aim_at_enemies);
}

inline bool aimbot_ignore_enabled(uint32_t flag) {
  return (config.aimbot.ignore & flag) != 0;
}

enum class aimbot_player_skip_reason {
  none,
  invalid,
  local,
  dormant,
  dead,
  invulnerable,
  ignored,
  friend_state,
  ipc_bot,
  cloaked,
  team,
  type
};

inline aimbot_player_skip_reason aimbot_player_skip_reason_for(Player* localplayer, Player* player) {
  if (localplayer == nullptr || player == nullptr) return aimbot_player_skip_reason::invalid;
  if (player == localplayer) return aimbot_player_skip_reason::local;
  if (player->is_dormant()) return aimbot_player_skip_reason::dormant;
  if (!player->is_alive()) return aimbot_player_skip_reason::dead;
  if (aimbot_ignore_enabled(Aim::ignore_invulnerable) && player->is_invulnerable()) return aimbot_player_skip_reason::invulnerable;
  if (player->is_ignored()) return aimbot_player_skip_reason::ignored;
  if (aimbot_ignore_enabled(Aim::ignore_friends) && player->is_friend()) return aimbot_player_skip_reason::friend_state;
  if (aimbot_ignore_enabled(Aim::ignore_cloaked) &&
      (player->in_cond(TF_COND_STEALTHED) ||
       player->in_cond(TF_COND_STEALTHED_BLINK) ||
       player->in_cond(TF_COND_STEALTHED_USER_BUFF) ||
       player->in_cond(TF_COND_STEALTHED_USER_BUFF_FADING))) {
    return aimbot_player_skip_reason::cloaked;
  }
  player_info pinfo{};
  if (aimbot_ignore_enabled(Aim::ignore_ipc_bots) &&
      engine != nullptr &&
      engine->get_player_info(player->get_index(), &pinfo) &&
      pinfo.friends_id != 0 &&
      pinfo.fakeplayer != true &&
      cat_ipc::client::is_local_ipc_friend(static_cast<std::uint32_t>(pinfo.friends_id))) {
    return aimbot_player_skip_reason::ipc_bot;
  }
  if (player->get_team() == localplayer->get_team() && !aimbot_is_friendlyfire_enabled()) return aimbot_player_skip_reason::team;
  if (!aimbot_should_target_player_type(player)) return aimbot_player_skip_reason::type;
  return aimbot_player_skip_reason::none;
}

inline bool aimbot_should_skip_player(Player* localplayer, Player* player) {
  return aimbot_player_skip_reason_for(localplayer, player) != aimbot_player_skip_reason::none;
}

inline bool aimbot_entity_is_enemy_owned(Player* localplayer, Entity* entity) {
  if (localplayer == nullptr || entity == nullptr) {
    return false;
  }

  Entity* owner = entity->get_owner_entity();
  if (owner != nullptr) {
    if (owner == localplayer || owner->get_team() == localplayer->get_team()) {
      return false;
    }

    return true;
  }

  const tf_team target_team = entity->get_team();
  return target_team == tf_team::UNKNOWN ||
         target_team == tf_team::SPECTATOR ||
         target_team != localplayer->get_team();
}

inline bool aimbot_is_stickybomb_target(Entity* entity) {
  if (entity == nullptr || entity->get_class_id() != class_id::PILL_OR_STICKY) {
    return false;
  }

  return strstr(entity->get_model_name(), "sticky") != nullptr;
}

inline bool aimbot_is_pumpkin_target(Entity* entity) {
  if (entity == nullptr) {
    return false;
  }

  return entity->is_network_class("CTFPumpkinBomb") ||
         strstr(entity->get_model_name(), "pumpkin_explode") != nullptr;
}

inline bool aimbot_should_skip_non_player_target(Player* localplayer, Entity* entity) {
  if (localplayer == nullptr || entity == nullptr || entity == localplayer) {
    return true;
  }

  if (entity->is_dormant()) {
    return true;
  }

  if (entity->is_building()) {
    if (!aimbot_aim_at_enabled(Aim::aim_at_buildings)) {
      return true;
    }

    auto* building = static_cast<Building*>(entity);
    return building->is_carried() ||
           building->get_health() <= 0 ||
           !aimbot_entity_is_enemy_owned(localplayer, entity);
  }

  if (aimbot_is_stickybomb_target(entity)) {
    return !aimbot_aim_at_enabled(Aim::aim_at_stickies) ||
           !aimbot_entity_is_enemy_owned(localplayer, entity);
  }

  if (aimbot_is_pumpkin_target(entity)) {
    return !aimbot_aim_at_enabled(Aim::aim_at_pumpkins);
  }

  return true;
}

inline Vec3 aimbot_entity_target_position(Entity* entity) {
  if (entity == nullptr) {
    return {};
  }

  const Vec3 mins = entity->get_collideable_mins();
  const Vec3 maxs = entity->get_collideable_maxs();
  const Vec3 center_offset = (mins + maxs) * 0.5f;
  return entity->get_collision_origin() + center_offset;
}

inline int aimbot_entity_health(Entity* entity) {
  if (entity == nullptr) {
    return 0;
  }

  if (entity->get_class_id() == class_id::PLAYER) {
    return static_cast<Player*>(entity)->get_health();
  }

  if (entity->is_building()) {
    return static_cast<Building*>(entity)->get_health();
  }

  return 1;
}

inline bool aimbot_entity_melee_reachable(Player* localplayer,
  Weapon* weapon,
  Entity* target,
  const Vec3& aim_angles) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr) {
    return false;
  }

  const float melee_range = aimbot_get_melee_range(localplayer, weapon, nullptr);
  const float melee_hull = aimbot_get_melee_hull(localplayer, weapon, nullptr);
  if (melee_range <= 0.0f || melee_hull < 0.0f) {
    return false;
  }

  Vec3 start = localplayer->get_shoot_pos();
  Vec3 forward = local_prediction_angles_to_direction(aim_angles);
  if (!aimbot_vec3_is_finite(start) || !aimbot_vec3_is_finite(forward)) {
    return false;
  }

  Vec3 end = start + (forward * melee_range);
  Vec3 zero_hull{};
  const Vec3 target_origin = target->get_collision_origin();
  const Vec3 target_mins = target->get_collideable_mins() + target_origin;
  const Vec3 target_maxs = target->get_collideable_maxs() + target_origin;

  float line_enter_fraction = 1.0f;
  const bool line_reaches_target = aimbot_segment_aabb_enter_fraction(
    start,
    end,
    target_mins,
    target_maxs,
    &line_enter_fraction);
  bool line_hit_target = false;
  const bool line_clear_to_target = aimbot_melee_trace_clear_to_entity(
    localplayer,
    target,
    start,
    end,
    zero_hull,
    zero_hull,
    line_reaches_target ? line_enter_fraction : 1.0f,
    &line_hit_target);
  if (line_hit_target || (line_reaches_target && line_clear_to_target)) {
    return true;
  }

  if (!line_clear_to_target) {
    return false;
  }

  const Vec3 hull{melee_hull, melee_hull, melee_hull};
  float hull_enter_fraction = 1.0f;
  if (!aimbot_segment_aabb_enter_fraction(start, end, target_mins - hull, target_maxs + hull, &hull_enter_fraction)) {
    return false;
  }

  Vec3 hull_mins{-melee_hull, -melee_hull, -melee_hull};
  Vec3 hull_maxs{melee_hull, melee_hull, melee_hull};
  return aimbot_melee_trace_clear_to_entity(localplayer, target, start, end, hull_mins, hull_maxs, hull_enter_fraction);
}

inline aimbot_candidate aimbot_find_non_player_candidate(Player* localplayer,
  Weapon* weapon,
  Entity* entity,
  const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  if (weapon == nullptr || aimbot_should_skip_non_player_target(localplayer, entity)) {
    return candidate;
  }

  const Vec3 target_position = aimbot_entity_target_position(entity);
  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  const Vec3 aim_angles = aimbot_calculate_angles_to_position(shoot_pos, target_position);
  if (weapon->is_flamethrower()) {
    const float hull_radius = projectile_flamethrower_hull_radius(weapon);
    if (distance_3d(shoot_pos, target_position) > projectile_flamethrower_effective_range(weapon) + hull_radius) {
      return candidate;
    }
  }
  if (weapon->is_melee() && !aimbot_entity_melee_reachable(localplayer, weapon, entity, aim_angles)) {
    return candidate;
  }

  candidate.entity = entity;
  candidate.aim_position = target_position;
  candidate.aim_angles = aim_angles;
  candidate.fov = aimbot_calculate_fov(aim_angles, original_view_angles);
  candidate.distance = distance_3d(localplayer->get_origin(), entity->get_origin());
  candidate.health = aimbot_entity_health(entity);
  candidate.visible = aimbot_trace_visible_to_position(localplayer, entity, target_position);
  return candidate;
}

inline bool aimbot_candidate_visible_shootable(Player* localplayer, const aimbot_candidate& candidate) {
  return localplayer != nullptr &&
         candidate.entity != nullptr &&
         candidate.visible &&
         localplayer->can_shoot(candidate.entity);
}

inline bool aimbot_use_key_active() {
  return config.aimbot.key.button == SDLK_UNKNOWN || is_button_active(config.aimbot.key);
}

inline bool aimbot_is_scoped_hitscan_rifle(Weapon* weapon) {
  return weapon != nullptr && weapon->is_sniper_rifle();
}

inline bool aimbot_weapon_requires_scope(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  if (weapon->get_def_id() == Sniper_m_TheMachina) {
    return true;
  }

  return attribute_manager != nullptr &&
    attribute_manager->attrib_hook_value(0, "sniper_only_fire_zoomed", weapon->to_entity()) != 0;
}

inline bool aimbot_modifier_enabled(uint32_t flag) {
  return (config.aimbot.hitscan_modifiers & flag) != 0;
}

inline bool aimbot_scoped_only_ready(Player* localplayer, Weapon* weapon) {
  if (localplayer == nullptr || weapon == nullptr) return false;
  const bool scoped_hitscan_rifle = aimbot_is_scoped_hitscan_rifle(weapon);
  if (localplayer->is_scoped()) {
    return true;
  }
  if (aimbot_weapon_requires_scope(weapon)) return false;
  if (!aimbot_modifier_enabled(Aim::hitscan_mod_scoped_only)) return true;
  return !scoped_hitscan_rifle;
}

inline bool aimbot_sniper_headshot_ready(Player* localplayer, Weapon* weapon) {
  if (localplayer == nullptr || weapon == nullptr) {
    return false;
  }

  if (weapon->can_fire_critical_shot(true)) {
    return true;
  }

  if (attribute_manager != nullptr && attribute_manager->attrib_hook_value(0, "sniper_no_headshot_without_full_charge", weapon->to_entity()) != 0) {
    return weapon->get_charged_damage() >= 150.0f;
  }

  if (attribute_manager != nullptr && attribute_manager->attrib_hook_value(0, "sniper_crit_no_scope", weapon->to_entity()) != 0) {
    return true;
  }

  return aimbot_sniper_scope_time_ready(localplayer);
}

inline bool aimbot_wait_for_headshot_ready(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_headshot) || localplayer == nullptr || weapon == nullptr) return true;
  if (candidate.player == nullptr || !weapon->is_headshot_weapon()) return true;

  // The wait only applies when we're actually trying for a headshot. If we
  // already settled on a body / limb (head occluded fallback, body-aim-if-lethal,
  // mask doesn't include head, etc.) there's nothing to wait for.
  if (candidate.hitbox != aim_hitbox_head) return true;

  if (weapon->is_sniper_rifle()) {
    return aimbot_sniper_headshot_ready(localplayer, weapon);
  }

  switch (weapon->get_weapon_id()) {
  case TF_WEAPON_REVOLVER:
    return attribute_manager == nullptr ||
      attribute_manager->attrib_hook_value(0, "set_weapon_mode", weapon->to_entity()) != 1 ||
      weapon->can_ambassador_headshot();
  default:
    return true;
  }
}

inline float aimbot_sniper_estimated_charge(Weapon* weapon) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  const float charge = weapon->get_charged_damage();
  return std::isfinite(charge) ? std::max(charge, 50.0f) : 50.0f;
}

inline int aimbot_sniper_estimated_damage(Player* localplayer,
  Weapon* weapon,
  Player* target,
  bool headshot) {
  if (weapon == nullptr || target == nullptr) {
    return 0;
  }

  const float base_damage = aimbot_sniper_estimated_charge(weapon);
  float multiplier = 1.0f;
  if (attribute_manager != nullptr) {
    multiplier = attribute_manager->attrib_hook_value(
      multiplier,
      headshot ? "headshot_damage_modify" : "mult_dmg",
      weapon->to_entity());
  }

  return static_cast<int>(std::ceil(base_damage * multiplier));
}

inline bool aimbot_revolver_lethal_body(Player* localplayer, Weapon* weapon, Player* target) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr) {
    return false;
  }

  const Vec3 origin_delta = target->get_origin() - localplayer->get_origin();
  const float dist = std::sqrt(
    (origin_delta.x * origin_delta.x) +
    (origin_delta.y * origin_delta.y) +
    (origin_delta.z * origin_delta.z));
  const float ramp = std::clamp((900.0f - dist) / (900.0f - 90.0f), 0.0f, 1.0f);
  const float base_damage = std::lerp(21.0f, 60.0f, ramp);
  float multiplier = 1.0f;
  if (attribute_manager != nullptr) {
    multiplier = attribute_manager->attrib_hook_value(multiplier, "mult_dmg", weapon->to_entity());
  }

  const int dmg = static_cast<int>(std::ceil(base_damage * multiplier));
  return target->get_health() <= dmg;
}

inline bool aimbot_body_aim_lethal(Player* localplayer, Weapon* weapon, Player* target) {
  if (!aimbot_modifier_enabled(Aim::hitscan_mod_body_aim_if_lethal) ||
      localplayer == nullptr ||
      weapon == nullptr ||
      target == nullptr ||
      !weapon->is_headshot_weapon()) {
    return false;
  }

  if (weapon->is_sniper_rifle()) {
    const int bodyshot_damage = aimbot_sniper_estimated_damage(localplayer, weapon, target, false);
    return target->get_health() > 0 && target->get_health() <= bodyshot_damage;
  }

  if (weapon->get_weapon_id() == TF_WEAPON_REVOLVER &&
      attribute_manager != nullptr &&
      attribute_manager->attrib_hook_value(0, "set_weapon_mode", weapon->to_entity()) == 1) {
    return aimbot_revolver_lethal_body(localplayer, weapon, target);
  }

  return false;
}

inline bool aimbot_wait_for_charge_ready(Player* localplayer,
  Weapon* weapon,
  const aimbot_candidate& candidate) {
  if (!aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_charge) ||
      localplayer == nullptr ||
      weapon == nullptr ||
      candidate.entity == nullptr) {
    return true;
  }

  if (!weapon->is_sniper_rifle() || !localplayer->is_scoped()) {
    return true;
  }

  const float charge = weapon->get_charged_damage();
  if (charge >= 150.0f) {
    return true;
  }

  if (candidate.player != nullptr) {
    const bool headshot_hitbox = candidate.hitbox == aim_hitbox_head;
    const int dmg = aimbot_sniper_estimated_damage(localplayer, weapon, candidate.player, headshot_hitbox);
    if (candidate.player->get_health() > 0 && candidate.player->get_health() <= dmg) {
      return true;
    }
    return false;
  }

  return charge >= 50.0f;
}

inline bool aimbot_is_projectile_weapon(Weapon* weapon) {
  if (weapon == nullptr) return false;
  if (weapon->is_flamethrower()) return true;

  switch (weapon->get_def_id()) {
  case Soldier_m_RocketLauncher:
  case Soldier_m_RocketLauncherR:
  case Soldier_m_TheDirectHit:
  case Soldier_m_TheBlackBox:
  case Soldier_m_RocketJumper:
  case Soldier_m_TheLibertyLauncher:
  case Soldier_m_TheCowMangler5000:
  case Soldier_m_TheOriginal:
  case Soldier_m_FestiveRocketLauncher:
  case Soldier_m_TheBeggarsBazooka:
  case Soldier_m_FestiveBlackBox:
  case Soldier_m_TheAirStrike:
  case Soldier_s_TheRighteousBison:
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
  case Engi_m_TheRescueRanger:
  case Engi_m_ThePomson6000:
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
  case Pyro_s_TheFlareGun:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheManmelter:
  case Pyro_s_TheScorchShot:
  case Pyro_s_FestiveFlareGun:
  case Pyro_m_DragonsFury:
  case Pyro_s_GasPasser:
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_TheLooseCannon:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
      return true;
  default:
      return false;
  }
}

inline bool aimbot_is_melee_weapon(Weapon* weapon) {
  return weapon != nullptr && weapon->is_melee();
}

inline float aimbot_effective_fov(const aimbot_candidate& candidate) {
  return candidate.preferred ? candidate.fov * 0.2f : candidate.fov;
}

inline bool aimbot_fov_unlimited() {
  return config.aimbot.fov <= 0.0f;
}

inline float aimbot_fov_limit(float scale = 1.0f, float minimum = 0.0f, float extra = 0.0f) {
  if (aimbot_fov_unlimited()) {
    return FLT_MAX;
  }

  return std::max(config.aimbot.fov * scale, minimum) + extra;
}

inline bool aimbot_fov_within_limit(float fov,
  float scale = 1.0f,
  float minimum = 0.0f,
  float extra = 0.0f) {
  return fov <= aimbot_fov_limit(scale, minimum, extra);
}

inline float aimbot_effective_distance(const aimbot_candidate& candidate) {
  return candidate.preferred ? candidate.distance * 0.35f : candidate.distance;
}

inline int aimbot_effective_smallest_health(const aimbot_candidate& candidate) {
  return candidate.preferred ? candidate.health - 500 : candidate.health;
}

inline int aimbot_effective_largest_health(const aimbot_candidate& candidate) {
  return candidate.preferred ? candidate.health + 500 : candidate.health;
}

inline bool aimbot_candidate_better(const aimbot_candidate& candidate, const aimbot_candidate& best) {
  if (candidate.entity == nullptr) return false;
  if (best.entity == nullptr) return true;

  switch (config.aimbot.target_type) {
  case Aim::TargetType::FOV:
    return aimbot_effective_fov(candidate) < aimbot_effective_fov(best);
  case Aim::TargetType::DISTANCE:
    if (aimbot_effective_distance(candidate) == aimbot_effective_distance(best)) {
      return aimbot_effective_fov(candidate) < aimbot_effective_fov(best);
    }
    return aimbot_effective_distance(candidate) < aimbot_effective_distance(best);
  case Aim::TargetType::LEAST_HEALTH:
    if (aimbot_effective_smallest_health(candidate) == aimbot_effective_smallest_health(best)) {
      return aimbot_effective_fov(candidate) < aimbot_effective_fov(best);
    }
    return aimbot_effective_smallest_health(candidate) < aimbot_effective_smallest_health(best);
  case Aim::TargetType::MOST_HEALTH:
    if (aimbot_effective_largest_health(candidate) == aimbot_effective_largest_health(best)) {
      return aimbot_effective_fov(candidate) < aimbot_effective_fov(best);
    }
    return aimbot_effective_largest_health(candidate) > aimbot_effective_largest_health(best);
  default:
    return false;
  }
}

inline float aimbot_candidate_target_speed(const aimbot_candidate& candidate) {
  if (candidate.player == nullptr) {
    return 0.0f;
  }

  Vec3 target_velocity = local_prediction_estimate_entity_velocity(candidate.player);
  if (local_prediction_vector_length(target_velocity) <= 0.001f) {
    target_velocity = candidate.player->get_velocity();
  }

  return local_prediction_velocity_2d_length(target_velocity) + (std::fabs(target_velocity.z) * 0.35f);
}

inline float aimbot_candidate_motion_scale(const aimbot_candidate& candidate) {
  const float target_speed = aimbot_candidate_target_speed(candidate);
  const float speed_ratio = std::clamp(target_speed / 320.0f, 0.0f, 1.75f);
  float motion_scale = 0.82f + (speed_ratio * 0.34f);

  if (candidate.projectile_direct || candidate.projectile_splash) {
    const float lead_scale = std::clamp(1.0f + (candidate.projectile_intercept_time * 0.28f), 1.0f, 1.45f);
    motion_scale *= lead_scale;
  }

  return std::clamp(motion_scale, 0.75f, 1.65f);
}

inline bool aimbot_mode_uses_visible_steering() {
  return config.aimbot.aim_mode == Aim::AimMode::SMOOTH ||
         config.aimbot.aim_mode == Aim::AimMode::ASSISTIVE;
}

inline float aimbot_projectile_visible_settle_fov(const aimbot_candidate& candidate) {
  if (!candidate.projectile_direct && !candidate.projectile_splash) {
    return 0.0f;
  }

  const float splash_extra = candidate.projectile_splash
    ? std::clamp(candidate.projectile_splash_radius / 110.0f, 0.5f, 2.0f)
    : 0.0f;
  const float lead_extra = std::clamp(candidate.projectile_intercept_time * 0.8f, 0.0f, 1.5f);
  const float miss_extra = std::clamp(candidate.projectile_miss_distance / 32.0f, 0.0f, 1.0f);
  return std::clamp(1.25f + splash_extra + lead_extra + miss_extra, 1.25f, 5.0f);
}

inline float aimbot_assist_strength(const Vec3& original_view_angles,
  const Vec3& target_view_angles,
  float motion_scale = 1.0f) {
  const float assist_strength = std::clamp(config.aimbot.assist_strength / 100.0f, 0.0f, 1.0f);
  if (assist_strength <= 0.0f) {
    return 0.0f;
  }

  const float aim_fov = aimbot_fov_limit(1.0f, 1.0f);
  const float fov_ratio = std::clamp(aimbot_calculate_fov(target_view_angles, original_view_angles) / aim_fov, 0.0f, 1.0f);
  const float close_ratio = 1.0f - fov_ratio;
  const float curve = std::clamp(1.0f - (fov_ratio * fov_ratio), 0.05f, 1.0f);
  const float close_boost = std::clamp(0.35f + (close_ratio * 0.65f), 0.05f, 1.0f);
  return std::clamp(
    assist_strength * curve * close_boost * std::clamp(motion_scale, 0.75f, 1.45f),
    0.0f,
    1.0f);
}

inline Vec3 aimbot_step_towards_angles(const Vec3& source_angles, const Vec3& target_angles, float max_step) {
  const Vec3 delta = aimbot_normalize_angle_delta(target_angles, source_angles);
  const float delta_length = std::hypot(delta.x, delta.y);
  if (delta_length <= 0.0001f || max_step <= 0.0f) {
    return aimbot_clamp_angles(target_angles);
  }

  const float scale = std::min(max_step / delta_length, 1.0f);
  return aimbot_clamp_angles(Vec3{
    source_angles.x + (delta.x * scale),
    source_angles.y + (delta.y * scale),
    0.0f
  });
}

inline Vec3 aimbot_apply_smooth_angles(const Vec3& source_view_angles,
  const Vec3& target_view_angles,
  float motion_scale = 1.0f) {
  const float smooth_factor = std::clamp(config.aimbot.smooth_factor, 1.0f, 30.0f);
  if (smooth_factor <= 1.001f) {
    return aimbot_clamp_angles(target_view_angles);
  }

  const Vec3 delta = aimbot_normalize_angle_delta(target_view_angles, source_view_angles);
  const float delta_length = std::hypot(delta.x, delta.y);
  if (delta_length <= 0.001f) {
    return aimbot_clamp_angles(target_view_angles);
  }

  const float tick_interval = global_vars != nullptr && global_vars->interval_per_tick > 0.0f
    ? global_vars->interval_per_tick
    : TICK_INTERVAL;
  const float smooth_ratio = (smooth_factor - 1.0f) / 29.0f;
  const float motion = std::clamp(motion_scale, 0.75f, 1.65f);
  const float response = std::lerp(42.0f, 8.0f, smooth_ratio) * motion;
  const float min_speed = std::lerp(420.0f, 65.0f, smooth_ratio) * motion;
  const float max_speed = std::lerp(2200.0f, 360.0f, smooth_ratio) * motion;
  const float snap_fov = std::lerp(0.035f, 0.18f, smooth_ratio);

  if (delta_length <= snap_fov) {
    return aimbot_clamp_angles(target_view_angles);
  }

  const float eased_step = delta_length * (1.0f - std::exp(-response * tick_interval));
  const float min_step = std::min(delta_length, min_speed * tick_interval);
  const float max_step = std::max(min_step, max_speed * tick_interval);
  const float step = std::clamp(eased_step, min_step, max_step);
  return aimbot_step_towards_angles(source_view_angles, target_view_angles, step);
}

inline Vec3 aimbot_apply_assistive_angles(const Vec3& source_view_angles,
  const Vec3& target_view_angles,
  const Vec3& last_input_angles,
  const bool has_last_input_angles,
  float motion_scale = 1.0f) {
  const Vec3 aim_delta = aimbot_normalize_angle_delta(target_view_angles, source_view_angles);
  const float aim_delta_length = std::hypot(aim_delta.x, aim_delta.y);
  if (aim_delta_length <= 0.001f) {
    return aimbot_clamp_angles(target_view_angles);
  }

  const float strength = aimbot_assist_strength(source_view_angles, target_view_angles, motion_scale);
  if (strength <= 0.0f) {
    return source_view_angles;
  }

  const float settle_fov = std::min(aimbot_fov_limit(0.15f, 1.5f), 6.0f);
  const float tick_interval = global_vars != nullptr && global_vars->interval_per_tick > 0.0f
    ? global_vars->interval_per_tick
    : TICK_INTERVAL;
  const auto settle_towards_target = [&]() -> Vec3 {
    if (aim_delta_length > settle_fov) {
      return source_view_angles;
    }

    const float settle_speed = std::lerp(24.0f, 130.0f, strength) * std::clamp(motion_scale, 0.75f, 1.65f);
    return aimbot_step_towards_angles(source_view_angles, target_view_angles, settle_speed * tick_interval);
  };

  if (!has_last_input_angles) {
    return settle_towards_target();
  }

  const Vec3 mouse_delta = aimbot_normalize_angle_delta(source_view_angles, last_input_angles);
  const Vec3 target_delta = aimbot_normalize_angle_delta(target_view_angles, last_input_angles);
  const float mouse_delta_length = std::hypot(mouse_delta.x, mouse_delta.y);
  const float target_delta_length = std::hypot(target_delta.x, target_delta.y);
  if (target_delta_length <= 0.0001f) {
    return aimbot_clamp_angles(target_view_angles);
  }

  if (mouse_delta_length <= 0.0001f) {
    return settle_towards_target();
  }

  const float alignment = ((mouse_delta.x * target_delta.x) + (mouse_delta.y * target_delta.y)) /
    std::max(mouse_delta_length * target_delta_length, 0.0001f);
  if (alignment <= -0.15f) {
    return source_view_angles;
  }

  const float limited_length = std::min(mouse_delta_length, target_delta_length);
  const Vec3 limited_target_delta{
    target_delta.x * (limited_length / target_delta_length),
    target_delta.y * (limited_length / target_delta_length),
    0.0f
  };
  const float alignment_scale = std::clamp((alignment + 0.15f) / 1.15f, 0.0f, 1.0f);
  const float blended_strength = strength * alignment_scale;
  const Vec3 blended_delta{
    mouse_delta.x + ((limited_target_delta.x - mouse_delta.x) * blended_strength),
    mouse_delta.y + ((limited_target_delta.y - mouse_delta.y) * blended_strength),
    0.0f
  };

  const Vec3 assisted_angles = aimbot_clamp_angles(Vec3{
    source_view_angles.x - mouse_delta.x + blended_delta.x,
    source_view_angles.y - mouse_delta.y + blended_delta.y,
    0.0f
  });

  if (aimbot_calculate_fov(assisted_angles, target_view_angles) > aim_delta_length + 0.01f) {
    return source_view_angles;
  }

  return assisted_angles;
}

inline Vec3 aimbot_apply_mode_angles(const Vec3& source_view_angles,
  const Vec3& target_view_angles,
  const Vec3& last_input_angles,
  const bool has_last_input_angles,
  const aimbot_candidate& candidate) {
  const float motion_scale = aimbot_candidate_motion_scale(candidate);
  switch (config.aimbot.aim_mode) {
  case Aim::AimMode::SMOOTH:
    return aimbot_apply_smooth_angles(source_view_angles, target_view_angles, motion_scale);
  case Aim::AimMode::ASSISTIVE:
    return aimbot_apply_assistive_angles(
      source_view_angles,
      target_view_angles,
      last_input_angles,
      has_last_input_angles,
      motion_scale);
  default:
    return target_view_angles;
  }
}

inline bool aimbot_simple_move_sim_valid(Player* localplayer, Player* target, float horizon_seconds) {
  if (localplayer == nullptr || target == nullptr) return false;

  Vec3 predicted_origin = local_prediction_predict_entity_origin(target, horizon_seconds);
  Vec3 predicted_angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), predicted_origin);
  float predicted_fov = aimbot_calculate_fov(predicted_angles, localplayer->get_punch_angles());
  return aimbot_fov_within_limit(predicted_fov, 1.25f, 4.0f) &&
         aimbot_trace_visible_to_position(localplayer, target, predicted_origin);
}

inline bool aimbot_simple_move_sim_valid_no_visibility(Player* localplayer, Player* target, float horizon_seconds) {
  if (localplayer == nullptr || target == nullptr) return false;

  Vec3 predicted_origin = local_prediction_predict_entity_origin(target, horizon_seconds);
  Vec3 predicted_angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), predicted_origin);
  float predicted_fov = aimbot_calculate_fov(predicted_angles, localplayer->get_punch_angles());
  return aimbot_fov_within_limit(predicted_fov, 1.25f, 4.0f);
}

inline bool aimbot_should_auto_scope(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!config.aimbot.auto_scope || localplayer == nullptr || weapon == nullptr || candidate.player == nullptr) return false;
  if (localplayer->get_tf_class() != tf_class::SNIPER || !weapon->is_sniper_rifle()) return false;
  if (aimbot::autoscope_scoped_state(localplayer) || !weapon->can_secondary_attack()) return false;
  if (!localplayer->is_on_ground()) return false;
  if (candidate.distance > config.aimbot.auto_scope_threshold) return false;

  // The candidate already carries a visible hitbox position chosen by hitscan_aim.
  // Trust that instead of re-tracing to the (often-occluded) feet origin.
  if (candidate.visible && aimbot_vec3_is_finite(candidate.aim_position)) {
    return true;
  }

  if (!candidate.visible) {
    return aimbot_simple_move_sim_valid_no_visibility(localplayer, candidate.player, 0.2f);
  }
  return aimbot_simple_move_sim_valid(localplayer, candidate.player, 0.2f);
}

inline bool aimbot_should_auto_unscope(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!config.aimbot.auto_unscope || localplayer == nullptr || weapon == nullptr || candidate.player == nullptr) return false;
  if (localplayer->get_tf_class() != tf_class::SNIPER || !weapon->is_sniper_rifle()) return false;
  if (!aimbot::autoscope_scoped_state(localplayer)) return false;

  float scoped_time = (localplayer->get_tickbase() * TICK_INTERVAL) - localplayer->get_fov_time();
  if (scoped_time < 0.15f) return false;
  if (aimbot_candidate_visible_shootable(localplayer, candidate)) return false;
  if (!candidate.visible) return false;
  if (candidate.distance <= config.aimbot.auto_scope_threshold) return true;
  return !aimbot_simple_move_sim_valid(localplayer, candidate.player, 0.15f);
}

inline bool aimbot_should_auto_rev(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!config.aimbot.auto_rev || localplayer == nullptr || weapon == nullptr || candidate.player == nullptr) return false;
  if (localplayer->get_tf_class() != tf_class::HEAVYWEAPONS || !weapon->is_minigun()) return false;
  if (localplayer->is_heavy_revved() || !weapon->can_secondary_attack()) return false;
  if (!localplayer->is_on_ground()) return false;
  if (!aimbot_candidate_visible_shootable(localplayer, candidate) && candidate.distance <= config.aimbot.auto_rev_threshold) return false;
  return aimbot_simple_move_sim_valid(localplayer, candidate.player, 0.15f);
}

inline bool aimbot_should_auto_unrev(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!config.aimbot.auto_unrev || localplayer == nullptr || weapon == nullptr || candidate.player == nullptr) return false;
  if (localplayer->get_tf_class() != tf_class::HEAVYWEAPONS || !weapon->is_minigun()) return false;
  if (!localplayer->is_heavy_revved()) return false;
  if (aimbot_candidate_visible_shootable(localplayer, candidate)) return false;
  if (candidate.distance <= config.aimbot.auto_rev_threshold) return true;
  return !aimbot_simple_move_sim_valid(localplayer, candidate.player, 0.1f);
}

#endif
