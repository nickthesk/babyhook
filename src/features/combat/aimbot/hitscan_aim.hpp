#ifndef HITSCAN_AIM_HPP
#define HITSCAN_AIM_HPP

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>

#include "aimbot.hpp"
#include "aim_utils.hpp"

#include "features/combat/backtrack/backtrack.hpp"

struct hitscan_settings_view {
  uint32_t hitbox_mask = aim_hitbox_mask_default_hitscan;
  bool scoped_only = false;
  bool wait_for_headshot = false;
  bool wait_for_charge = false;
  bool body_aim_if_lethal = false;
  bool spread_compensation = false;
  float multipoint_scale = 0.0f;
};

struct hitscan_point {
  bool valid = false;
  int bone = 0;
  int hitbox = -1;
  int priority = 0;
  Vec3 position{};
  Vec3 angles{};
  float fov = FLT_MAX;
};

struct hitscan_target {
  aimbot_candidate candidate{};
  bool ready = false;
};

struct hitscan_trace_result {
  bool hit = false;
  bool clear = false;
  Entity* entity = nullptr;
  int hitbox = -1;
};

using hitscan_aim_trace_result = hitscan_trace_result;

inline Vec3 hitscan_aim_bullet_angles(Player* localplayer, const Vec3& view_angles) {
  return localplayer != nullptr ? view_angles + localplayer->get_punch_angles() : view_angles;
}

inline Vec3 hitscan_aim_command_angles(Player* localplayer, const Vec3& bullet_angles) {
  return localplayer != nullptr ? bullet_angles - localplayer->get_punch_angles() : bullet_angles;
}

inline uint32_t hitscan_aim_configured_hitbox_mask() {
  const uint32_t mask = config.aimbot.hitscan_hitboxes & aim_hitbox_mask_all;
  return mask != 0 ? mask : aim_hitbox_mask_default_hitscan;
}

inline unsigned int hitscan_aim_trace_mask() {
  unsigned int trace_mask = MASK_SHOT | CONTENTS_GRATE;
  if (config.aimbot.shoot_through_glass) {
    trace_mask &= ~CONTENTS_WINDOW;
  }

  return trace_mask;
}

inline hitscan_settings_view hitscan_aim_settings() {
  return {
    .hitbox_mask = hitscan_aim_configured_hitbox_mask(),
    .scoped_only = aimbot_modifier_enabled(Aim::hitscan_mod_scoped_only),
    .wait_for_headshot = aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_headshot),
    .wait_for_charge = aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_charge),
    .body_aim_if_lethal = aimbot_modifier_enabled(Aim::hitscan_mod_body_aim_if_lethal),
    .spread_compensation = config.aimbot.spread_compensation,
    .multipoint_scale = std::clamp(config.aimbot.multipoint_scale, 0.0f, 100.0f)
  };
}

inline bool hitscan_aim_world_clear(const Vec3& start_pos, const Vec3& end_pos) {
  if (engine_trace == nullptr || !aimbot_vec3_is_finite(start_pos) || !aimbot_vec3_is_finite(end_pos)) {
    return false;
  }

  Vec3 start = start_pos;
  Vec3 end = end_pos;
  ray_t ray = engine_trace->init_ray(&start, &end);
  trace_filter filter{};
  engine_trace->init_world_trace_filter(&filter);
  trace_t trace{};
  engine_trace->trace_ray(&ray, hitscan_aim_trace_mask(), &filter, &trace);
  return !trace.all_solid && !trace.start_solid && trace.fraction >= 0.999f;
}

inline hitscan_trace_result hitscan_aim_trace_line(Player* localplayer, const Vec3& start_pos, const Vec3& end_pos) {
  hitscan_trace_result result{};
  if (engine_trace == nullptr || localplayer == nullptr) {
    return result;
  }

  Vec3 start = start_pos;
  Vec3 end = end_pos;
  ray_t ray = engine_trace->init_ray(&start, &end);
  trace_filter filter{};
  engine_trace->init_trace_filter(&filter, localplayer);
  trace_t trace{};
  engine_trace->trace_ray(&ray, hitscan_aim_trace_mask(), &filter, &trace);

  result.entity = static_cast<Entity*>(trace.entity);
  result.hitbox = trace.hitbox;
  result.clear = !trace.all_solid && !trace.start_solid && trace.fraction >= 0.999f;
  result.hit = result.entity != nullptr || result.clear;
  return result;
}

inline bool hitscan_aim_trace_point(Player* localplayer,
  Entity* target,
  const Vec3& point,
  hitscan_trace_result* result_out = nullptr) {
  if (localplayer == nullptr || target == nullptr || !aimbot_vec3_is_finite(point)) {
    return false;
  }

  hitscan_trace_result result = hitscan_aim_trace_line(localplayer, localplayer->get_shoot_pos(), point);
  if (result_out != nullptr) {
    *result_out = result;
  }

  return result.entity == target || result.clear;
}

inline bool hitscan_aim_body_forced(Player* localplayer, Weapon* weapon, Player* target) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr) {
    return false;
  }

  if (!weapon->is_headshot_weapon()) {
    return true;
  }

  if (weapon->is_sniper_rifle() && !localplayer->is_scoped() && weapon->get_charged_damage() <= 0.0f) {
    return true;
  }

  if (aimbot_modifier_enabled(Aim::hitscan_mod_body_aim_if_lethal) &&
      aimbot_body_aim_lethal(localplayer, weapon, target)) {
    return true;
  }

  return false;
}

inline int hitscan_aim_first_hitbox_for_mask(uint32_t mask, const std::array<int, 18>& order) {
  for (const int hitbox : order) {
    if (aimbot_hitbox_matches_mask(hitbox, mask)) {
      return hitbox;
    }
  }

  return -1;
}

inline int hitscan_aim_priority_hitbox(Player* localplayer,
  Weapon* weapon,
  Player* target,
  const hitscan_settings_view& settings) {
  constexpr std::array<int, 18> body_order{
    aim_hitbox_spine_3,
    aim_hitbox_spine_2,
    aim_hitbox_spine_1,
    aim_hitbox_spine_0,
    aim_hitbox_pelvis,
    aim_hitbox_left_upper_arm,
    aim_hitbox_right_upper_arm,
    aim_hitbox_left_thigh,
    aim_hitbox_right_thigh,
    aim_hitbox_left_forearm,
    aim_hitbox_right_forearm,
    aim_hitbox_left_calf,
    aim_hitbox_right_calf,
    aim_hitbox_left_hand,
    aim_hitbox_right_hand,
    aim_hitbox_left_foot,
    aim_hitbox_right_foot,
    aim_hitbox_head
  };
  constexpr std::array<int, 18> head_order{
    aim_hitbox_head,
    aim_hitbox_spine_3,
    aim_hitbox_spine_2,
    aim_hitbox_spine_1,
    aim_hitbox_spine_0,
    aim_hitbox_pelvis,
    aim_hitbox_left_upper_arm,
    aim_hitbox_right_upper_arm,
    aim_hitbox_left_thigh,
    aim_hitbox_right_thigh,
    aim_hitbox_left_forearm,
    aim_hitbox_right_forearm,
    aim_hitbox_left_calf,
    aim_hitbox_right_calf,
    aim_hitbox_left_hand,
    aim_hitbox_right_hand,
    aim_hitbox_left_foot,
    aim_hitbox_right_foot
  };

  if (hitscan_aim_body_forced(localplayer, weapon, target)) {
    uint32_t body_mask = settings.hitbox_mask & ~aim_hitbox_mask_head;
    if (body_mask == 0) {
      body_mask = aim_hitbox_mask_body | aim_hitbox_mask_pelvis;
    }
    return hitscan_aim_first_hitbox_for_mask(body_mask, body_order);
  }

  const bool head_ready = aimbot_headshot_ready_for_priority(localplayer, weapon);
  if (weapon != nullptr &&
      weapon->is_headshot_weapon() &&
      (settings.wait_for_headshot || head_ready) &&
      (settings.hitbox_mask & aim_hitbox_mask_head) != 0) {
    return aim_hitbox_head;
  }

  return hitscan_aim_first_hitbox_for_mask(settings.hitbox_mask, body_order);
}

inline int hitscan_aim_build_hitbox_order(int priority_hitbox,
  const hitscan_settings_view& settings,
  std::array<int, 18>* order_out) {
  if (order_out == nullptr || priority_hitbox < 0) {
    return 0;
  }

  constexpr std::array<int, 18> fallback_order{
    aim_hitbox_head,
    aim_hitbox_spine_3,
    aim_hitbox_spine_2,
    aim_hitbox_spine_1,
    aim_hitbox_spine_0,
    aim_hitbox_pelvis,
    aim_hitbox_left_upper_arm,
    aim_hitbox_right_upper_arm,
    aim_hitbox_left_thigh,
    aim_hitbox_right_thigh,
    aim_hitbox_left_forearm,
    aim_hitbox_right_forearm,
    aim_hitbox_left_calf,
    aim_hitbox_right_calf,
    aim_hitbox_left_hand,
    aim_hitbox_right_hand,
    aim_hitbox_left_foot,
    aim_hitbox_right_foot
  };

  int count = 0;
  auto add_hitbox = [&](int hitbox) {
    if (!aimbot_hitbox_matches_mask(hitbox, settings.hitbox_mask)) {
      return;
    }
    for (int index = 0; index < count; ++index) {
      if ((*order_out)[index] == hitbox) {
        return;
      }
    }
    (*order_out)[count++] = hitbox;
  };

  add_hitbox(priority_hitbox);
  for (const int hitbox : fallback_order) {
    add_hitbox(hitbox);
  }
  return count;
}

inline bool hitscan_aim_accepts_trace_hitbox(const aimbot_candidate& candidate, int trace_hitbox) {
  if (candidate.player == nullptr || candidate.hitbox < 0) {
    return true;
  }

  if (candidate.hitbox == aim_hitbox_head) {
    return trace_hitbox == aim_hitbox_head;
  }

  return trace_hitbox == candidate.hitbox ||
    aimbot_hitbox_matches_mask(trace_hitbox, hitscan_aim_configured_hitbox_mask());
}

inline bool hitscan_aim_adjust_head_point(Player* localplayer, Player* target, Vec3* point) {
  if (localplayer == nullptr || target == nullptr || point == nullptr) {
    return false;
  }

  Vec3 adjusted = *point;
  for (int index = 0; index < 8; ++index) {
    hitscan_trace_result trace{};
    if (!hitscan_aim_trace_point(localplayer, target, adjusted, &trace)) {
      return false;
    }
    if (trace.hitbox == aim_hitbox_head) {
      *point = adjusted;
      return true;
    }
    adjusted.z += 1.0f;
  }

  return false;
}

inline hitscan_point hitscan_aim_make_point(Player* localplayer,
  Player* target,
  Weapon* weapon,
  const Vec3& view_angles,
  int hitbox,
  int bone,
  int priority,
  Vec3 position) {
  hitscan_point point{};
  if (localplayer == nullptr || target == nullptr || !aimbot_vec3_is_finite(position)) {
    return point;
  }

  hitscan_trace_result trace{};
  if (!hitscan_aim_trace_point(localplayer, target, position, &trace)) {
    return point;
  }

  if (hitbox == aim_hitbox_head &&
      trace.entity == target &&
      trace.hitbox != aim_hitbox_head &&
      !hitscan_aim_adjust_head_point(localplayer, target, &position)) {
    return point;
  }

  point.valid = true;
  point.bone = bone;
  point.hitbox = hitbox;
  point.priority = priority;
  point.position = position;
  point.angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), position);
  point.fov = aimbot_calculate_fov(hitscan_aim_command_angles(localplayer, point.angles), view_angles);
  return point;
}

inline hitscan_point hitscan_aim_find_point(Player* localplayer,
  Weapon* weapon,
  Player* target,
  const Vec3& view_angles) {
  hitscan_point best{};
  if (localplayer == nullptr || weapon == nullptr || target == nullptr || model_info == nullptr) {
    return best;
  }

  const hitscan_settings_view settings = hitscan_aim_settings();
  const int priority_hitbox = hitscan_aim_priority_hitbox(localplayer, weapon, target, settings);
  std::array<int, 18> hitboxes{};
  const int hitbox_count = hitscan_aim_build_hitbox_order(priority_hitbox, settings, &hitboxes);
  if (hitbox_count <= 0) {
    return best;
  }

  const model_t* model = target->get_model();
  studio_hdr* hdr = model != nullptr ? model_info->get_studio_model(model) : nullptr;
  studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(target->get_hitbox_set()) : nullptr;
  if (hitbox_set == nullptr) {
    return best;
  }

  matrix_3x4 bone_to_world[128]{};
  if (!target->setup_bones(bone_to_world, 128, 0x7FF00, target->get_simulation_time())) {
    for (int order_index = 0; order_index < hitbox_count; ++order_index) {
      const int hitbox_id = hitboxes[order_index];
      Vec3 position{};
      int bone = 0;
      if (!target->get_hitbox_center(hitbox_id, &position, &bone)) {
        continue;
      }

      hitscan_point point = hitscan_aim_make_point(
        localplayer,
        target,
        weapon,
        view_angles,
        hitbox_id,
        bone,
        order_index,
        position);
      if (!point.valid) {
        continue;
      }

      if (!best.valid || point.priority < best.priority || (point.priority == best.priority && point.fov < best.fov)) {
        best = point;
      }
    }
    return best;
  }

  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  for (int order_index = 0; order_index < hitbox_count; ++order_index) {
    const int hitbox_id = hitboxes[order_index];
    if (hitbox_id < 0 || hitbox_id >= hitbox_set->num_hitboxes) {
      continue;
    }

    studio_box* hitbox = hitbox_set->hitbox(hitbox_id);
    if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= 128) {
      continue;
    }

    constexpr int max_local_points = 10;
    Vec3 local_points[max_local_points]{};
    const bool use_multipoint = hitbox_id == priority_hitbox && settings.multipoint_scale > 0.0f;
    const int point_count = aimbot_build_local_hitbox_points(
      *hitbox,
      bone_to_world[hitbox->bone],
      shoot_pos,
      local_points,
      max_local_points,
      use_multipoint);

    for (int point_index = 0; point_index < point_count; ++point_index) {
      const Vec3 position = aimbot_transform_point(local_points[point_index], bone_to_world[hitbox->bone]);
      hitscan_point point = hitscan_aim_make_point(
        localplayer,
        target,
        weapon,
        view_angles,
        hitbox_id,
        hitbox->bone,
        order_index,
        position);
      if (!point.valid) {
        continue;
      }

      if (!best.valid || point.priority < best.priority || (point.priority == best.priority && point.fov < best.fov)) {
        best = point;
      }
    }

    if (best.valid && best.hitbox == priority_hitbox) {
      break;
    }
  }

  return best;
}

inline aimbot_candidate hitscan_aim_make_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const hitscan_point& point,
  const Vec3& view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr || weapon == nullptr || player == nullptr || !point.valid) {
    return candidate;
  }

  candidate.entity = player;
  candidate.player = player;
  candidate.preferred = aimbot::has_preference(player);
  candidate.bone = point.bone;
  candidate.hitbox = point.hitbox;
  candidate.aim_position = point.position;
  candidate.aim_angles = point.angles;
  candidate.fov = point.fov;
  candidate.distance = distance_3d(localplayer->get_origin(), player->get_origin());
  candidate.health = player->get_health();
  candidate.simulation_time = player->get_simulation_time();
  candidate.tick_count = local_prediction_time_to_ticks(candidate.simulation_time + backtrack::interpolation_time());
  candidate.command_angles = hitscan_aim_command_angles(localplayer, point.angles);
  candidate.visible = true;
  return candidate;
}

inline aimbot_candidate hitscan_aim_find_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& view_angles) {
  return hitscan_aim_make_candidate(
    localplayer,
    weapon,
    player,
    hitscan_aim_find_point(localplayer, weapon, player, view_angles),
    view_angles);
}

inline aimbot_candidate hitscan_aim_find_occluded_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& view_angles) {
  return hitscan_aim_find_candidate(localplayer, weapon, player, view_angles);
}

inline bool hitscan_aim_trace_backtrack_candidate(const aimbot_candidate& candidate,
  const Vec3& start_pos,
  const Vec3& end_pos) {
  if (!candidate.backtrack || !candidate.visible) {
    return false;
  }

  if (!hitscan_aim_world_clear(start_pos, candidate.aim_position)) {
    return false;
  }

  if (candidate.hitbox < 0) {
    return aimbot_segment_intersects_aabb(start_pos, end_pos, candidate.backtrack_mins, candidate.backtrack_maxs);
  }

  const Vec3 center = (candidate.backtrack_mins + candidate.backtrack_maxs) * 0.5f;
  const float height = std::max(candidate.backtrack_maxs.z - candidate.backtrack_mins.z, 1.0f);
  Vec3 mins = candidate.backtrack_mins;
  Vec3 maxs = candidate.backtrack_maxs;

  switch (candidate.hitbox) {
  case aim_hitbox_head:
    mins.z = center.z + (height * 0.22f);
    break;
  case aim_hitbox_pelvis:
  case aim_hitbox_spine_0:
    maxs.z = center.z + (height * 0.08f);
    break;
  case aim_hitbox_left_thigh:
  case aim_hitbox_left_calf:
  case aim_hitbox_left_foot:
  case aim_hitbox_right_thigh:
  case aim_hitbox_right_calf:
  case aim_hitbox_right_foot:
    maxs.z = center.z;
    break;
  default:
    mins.z = candidate.backtrack_mins.z + (height * 0.26f);
    maxs.z = candidate.backtrack_mins.z + (height * 0.82f);
    break;
  }

  return aimbot_segment_intersects_aabb(start_pos, end_pos, mins, maxs);
}

inline bool hitscan_aim_trace_candidate(Player* localplayer,
  const aimbot_candidate& candidate,
  const Vec3& command_view_angles,
  const Vec3& spread_offset = {},
  bool use_spread = false,
  hitscan_trace_result* result = nullptr) {
  if (result != nullptr) {
    *result = {};
  }

  if (localplayer == nullptr ||
      candidate.entity == nullptr ||
      engine_trace == nullptr ||
      !aimbot_vec3_is_finite(candidate.aim_position)) {
    return false;
  }

  Vec3 start_pos = localplayer->get_shoot_pos();
  const Vec3 bullet_angles = hitscan_aim_bullet_angles(localplayer, command_view_angles);
  Vec3 forward{};
  Vec3 right{};
  Vec3 up{};
  angle_vectors(bullet_angles, &forward, &right, &up);
  if (!aimbot_vec3_is_finite(forward)) {
    return false;
  }

  if (use_spread) {
    forward = local_prediction_normalize(forward + (right * spread_offset.x) + (up * spread_offset.y));
    if (!aimbot_vec3_is_finite(forward)) {
      return false;
    }
  }

  const float trace_distance = std::max(distance_3d(start_pos, candidate.aim_position) + 96.0f, 256.0f);
  Vec3 end_pos = start_pos + (forward * trace_distance);
  hitscan_trace_result trace = hitscan_aim_trace_line(localplayer, start_pos, end_pos);
  if (result != nullptr) {
    *result = trace;
  }

  if (trace.entity == candidate.entity && hitscan_aim_accepts_trace_hitbox(candidate, trace.hitbox)) {
    if (result != nullptr) {
      result->hit = true;
    }
    return true;
  }

  if (hitscan_aim_trace_backtrack_candidate(candidate, start_pos, end_pos)) {
    if (result != nullptr) {
      result->hit = true;
      result->entity = candidate.entity;
      result->hitbox = candidate.hitbox;
    }
    return true;
  }

  return false;
}

inline bool hitscan_aim_headshot_ready(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  return aimbot_wait_for_headshot_ready(localplayer, weapon, candidate);
}

inline bool hitscan_aim_charge_ready(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  return aimbot_wait_for_charge_ready(localplayer, weapon, candidate);
}

#endif
