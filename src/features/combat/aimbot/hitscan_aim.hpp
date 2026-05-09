/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/combat/aimbot/hitscan_aim.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef HITSCAN_AIM_HPP
#define HITSCAN_AIM_HPP

#include <algorithm>
#include <cstdint>

#include "aim_utils.hpp"

#include "features/automation/nographics/nographics.hpp"

struct hitscan_aim_bounds {
  bool valid = false;
  Vec3 mins{};
  Vec3 maxs{};
};

struct hitscan_aim_bounds_zone {
  bool valid = false;
  Vec3 mins{};
  Vec3 maxs{};
};

struct hitscan_aim_trace_result {
  bool hit = false;
  Entity* entity = nullptr;
  int hitbox = -1;
};

inline Vec3 hitscan_aim_bullet_angles(Player* localplayer, const Vec3& view_angles) {
  if (localplayer == nullptr) {
    return view_angles;
  }

  return view_angles + localplayer->get_punch_angles();
}

inline Vec3 hitscan_aim_command_angles(Player* localplayer, const Vec3& bullet_angles) {
  if (localplayer == nullptr) {
    return bullet_angles;
  }

  return bullet_angles - localplayer->get_punch_angles();
}

inline uint32_t hitscan_aim_configured_hitbox_mask() {
  const uint32_t configured_mask = config.aimbot.hitscan_hitboxes & aim_hitbox_mask_all;
  return configured_mask != 0 ? configured_mask : aim_hitbox_mask_default_hitscan;
}

inline bool hitscan_aim_should_try_head_first(Player* localplayer, Weapon* weapon, uint32_t hitbox_mask) {
  return localplayer != nullptr &&
    weapon != nullptr &&
    (hitbox_mask & aim_hitbox_mask_head) != 0 &&
    weapon->is_headshot_weapon() &&
    (config.aimbot.wait_for_headshot || aimbot_headshot_ready_for_priority(localplayer, weapon));
}

inline unsigned int hitscan_aim_world_trace_mask() {
  unsigned int trace_mask = MASK_SHOT | CONTENTS_GRATE;
  if (config.aimbot.shoot_through_glass) {
    trace_mask &= ~CONTENTS_WINDOW;
  }

  return trace_mask;
}

inline bool hitscan_aim_world_clear(const Vec3& start_pos, const Vec3& end_pos) {
  if (engine_trace == nullptr || !aimbot_vec3_is_finite(start_pos) || !aimbot_vec3_is_finite(end_pos)) {
    return false;
  }

  Vec3 start = start_pos;
  Vec3 end = end_pos;
  struct ray_t ray = engine_trace->init_ray(&start, &end);
  struct trace_filter filter;
  engine_trace->init_world_trace_filter(&filter);

  struct trace_t trace_world{};
  engine_trace->trace_ray(&ray, hitscan_aim_world_trace_mask(), &filter, &trace_world);
  return !trace_world.all_solid && !trace_world.start_solid && trace_world.fraction >= 0.999f;
}

inline hitscan_aim_bounds hitscan_aim_get_player_bounds(Player* target, float expansion = 0.0f) {
  hitscan_aim_bounds bounds{};
  if (target == nullptr) {
    return bounds;
  }

  const Vec3 origin = target->get_collision_origin();
  const Vec3 mins = target->get_collideable_mins() + origin - Vec3{expansion, expansion, expansion};
  const Vec3 maxs = target->get_collideable_maxs() + origin + Vec3{expansion, expansion, expansion};
  if (!aimbot_vec3_is_finite(mins) || !aimbot_vec3_is_finite(maxs)) {
    return bounds;
  }

  if (maxs.x <= mins.x + 1.0f || maxs.y <= mins.y + 1.0f || maxs.z <= mins.z + 8.0f) {
    return bounds;
  }

  bounds.valid = true;
  bounds.mins = mins;
  bounds.maxs = maxs;
  return bounds;
}

inline float hitscan_aim_bounds_height(const hitscan_aim_bounds& bounds) {
  return bounds.valid ? std::max(bounds.maxs.z - bounds.mins.z, 1.0f) : 1.0f;
}

inline Vec3 hitscan_aim_bounds_center(const hitscan_aim_bounds& bounds) {
  return (bounds.mins + bounds.maxs) * 0.5f;
}

inline Vec3 hitscan_aim_bounds_point_for_hitbox(const hitscan_aim_bounds& bounds, int hitbox_id) {
  const Vec3 center = hitscan_aim_bounds_center(bounds);
  const float height = hitscan_aim_bounds_height(bounds);
  float z_ratio = 0.58f;

  switch (hitbox_id) {
  case aim_hitbox_head:
    z_ratio = 0.86f;
    break;
  case aim_hitbox_pelvis:
    z_ratio = 0.42f;
    break;
  case aim_hitbox_spine_0:
    z_ratio = 0.50f;
    break;
  case aim_hitbox_spine_1:
    z_ratio = 0.58f;
    break;
  case aim_hitbox_spine_2:
    z_ratio = 0.64f;
    break;
  case aim_hitbox_spine_3:
    z_ratio = 0.70f;
    break;
  case aim_hitbox_left_thigh:
  case aim_hitbox_left_calf:
  case aim_hitbox_left_foot:
  case aim_hitbox_right_thigh:
  case aim_hitbox_right_calf:
  case aim_hitbox_right_foot:
    z_ratio = 0.25f;
    break;
  default:
    z_ratio = 0.58f;
    break;
  }

  return Vec3{
    center.x,
    center.y,
    bounds.mins.z + (height * z_ratio)
  };
}

inline hitscan_aim_bounds_zone hitscan_aim_scaled_bounds_zone(const hitscan_aim_bounds& bounds,
  float xy_scale,
  float z_min_ratio,
  float z_max_ratio) {
  hitscan_aim_bounds_zone zone{};
  if (!bounds.valid) {
    return zone;
  }

  const Vec3 center = hitscan_aim_bounds_center(bounds);
  const float half_x = std::max((bounds.maxs.x - bounds.mins.x) * 0.5f * xy_scale, 3.0f);
  const float half_y = std::max((bounds.maxs.y - bounds.mins.y) * 0.5f * xy_scale, 3.0f);
  const float height = hitscan_aim_bounds_height(bounds);
  zone.valid = true;
  zone.mins = Vec3{
    center.x - half_x,
    center.y - half_y,
    bounds.mins.z + (height * std::clamp(z_min_ratio, 0.0f, 1.0f))
  };
  zone.maxs = Vec3{
    center.x + half_x,
    center.y + half_y,
    bounds.mins.z + (height * std::clamp(z_max_ratio, 0.0f, 1.0f))
  };
  return zone;
}

inline hitscan_aim_bounds_zone hitscan_aim_bounds_zone_for_hitbox(const hitscan_aim_bounds& bounds, int hitbox_id) {
  switch (hitbox_id) {
  case aim_hitbox_head:
    return hitscan_aim_scaled_bounds_zone(bounds, 0.28f, 0.76f, 1.0f);
  case aim_hitbox_pelvis:
    return hitscan_aim_scaled_bounds_zone(bounds, 0.48f, 0.28f, 0.52f);
  case aim_hitbox_spine_0:
    return hitscan_aim_scaled_bounds_zone(bounds, 0.48f, 0.36f, 0.60f);
  case aim_hitbox_spine_1:
  case aim_hitbox_spine_2:
  case aim_hitbox_spine_3:
    return hitscan_aim_scaled_bounds_zone(bounds, 0.46f, 0.46f, 0.78f);
  case aim_hitbox_left_upper_arm:
  case aim_hitbox_left_forearm:
  case aim_hitbox_left_hand:
  case aim_hitbox_right_upper_arm:
  case aim_hitbox_right_forearm:
  case aim_hitbox_right_hand:
    return hitscan_aim_scaled_bounds_zone(bounds, 0.58f, 0.42f, 0.78f);
  case aim_hitbox_left_thigh:
  case aim_hitbox_left_calf:
  case aim_hitbox_left_foot:
  case aim_hitbox_right_thigh:
  case aim_hitbox_right_calf:
  case aim_hitbox_right_foot:
    return hitscan_aim_scaled_bounds_zone(bounds, 0.42f, 0.02f, 0.44f);
  default:
    return hitscan_aim_scaled_bounds_zone(bounds, 0.65f, 0.08f, 0.92f);
  }
}

inline bool hitscan_aim_ray_hits_bounds_zone(const hitscan_aim_bounds_zone& zone,
  const Vec3& start_pos,
  const Vec3& end_pos) {
  return zone.valid && aimbot_segment_intersects_aabb(start_pos, end_pos, zone.mins, zone.maxs);
}

inline bool hitscan_aim_ray_hits_hitbox(Player* target, int hitbox_id, const Vec3& start_pos, const Vec3& end_pos) {
  if (target == nullptr || hitbox_id < 0 || model_info == nullptr) {
    return false;
  }

  const model_t* model = target->get_model();
  studio_hdr* hdr = model != nullptr ? model_info->get_studio_model(model) : nullptr;
  studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(target->get_hitbox_set()) : nullptr;
  if (hitbox_set == nullptr || hitbox_id >= hitbox_set->num_hitboxes) {
    return false;
  }

  studio_box* hitbox = hitbox_set->hitbox(hitbox_id);
  if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= 128) {
    return false;
  }

  matrix_3x4 bone_to_world[128]{};
  if (!target->setup_bones(bone_to_world, 128, 0x100, target->get_simulation_time())) {
    return false;
  }

  const Vec3 local_start = aimbot_inverse_transform_point(start_pos, bone_to_world[hitbox->bone]);
  const Vec3 local_end = aimbot_inverse_transform_point(end_pos, bone_to_world[hitbox->bone]);
  constexpr float hitbox_expansion = 1.25f;
  const Vec3 mins = hitbox->bbmin - Vec3{hitbox_expansion, hitbox_expansion, hitbox_expansion};
  const Vec3 maxs = hitbox->bbmax + Vec3{hitbox_expansion, hitbox_expansion, hitbox_expansion};
  return aimbot_segment_intersects_aabb(local_start, local_end, mins, maxs);
}

inline bool hitscan_aim_ray_hits_player_bounds(Player* target, const Vec3& start_pos, const Vec3& end_pos) {
  const hitscan_aim_bounds bounds = hitscan_aim_get_player_bounds(target, 1.5f);
  return bounds.valid && aimbot_segment_intersects_aabb(start_pos, end_pos, bounds.mins, bounds.maxs);
}

inline bool hitscan_aim_ray_hits_entity_bounds(Entity* target, const Vec3& start_pos, const Vec3& end_pos) {
  if (target == nullptr) {
    return false;
  }

  const Vec3 origin = target->get_collision_origin();
  const Vec3 mins = target->get_collideable_mins() + origin - Vec3{1.5f, 1.5f, 1.5f};
  const Vec3 maxs = target->get_collideable_maxs() + origin + Vec3{1.5f, 1.5f, 1.5f};
  if (!aimbot_vec3_is_finite(mins) || !aimbot_vec3_is_finite(maxs)) {
    return false;
  }

  return aimbot_segment_intersects_aabb(start_pos, end_pos, mins, maxs);
}

inline bool hitscan_aim_ray_hits_selected_area(Player* target,
  int hitbox_id,
  const Vec3& start_pos,
  const Vec3& end_pos) {
  if (target == nullptr) {
    return false;
  }

  if (hitbox_id >= 0 && hitscan_aim_ray_hits_hitbox(target, hitbox_id, start_pos, end_pos)) {
    return true;
  }

  if (hitbox_id < 0) {
    return hitscan_aim_ray_hits_player_bounds(target, start_pos, end_pos);
  }

  const hitscan_aim_bounds bounds = hitscan_aim_get_player_bounds(target, 1.5f);
  return hitscan_aim_ray_hits_bounds_zone(
    hitscan_aim_bounds_zone_for_hitbox(bounds, hitbox_id),
    start_pos,
    end_pos);
}

inline bool hitscan_aim_point_visible_by_fallback(Player* localplayer,
  Player* player,
  int hitbox_id,
  const Vec3& point) {
  if (!nographics::should_use_aimbot_trace_fallback() ||
      localplayer == nullptr ||
      player == nullptr ||
      !aimbot_vec3_is_finite(point)) {
    return false;
  }

  const Vec3 start_pos = localplayer->get_shoot_pos();
  if (!aimbot_vec3_is_finite(start_pos) || !hitscan_aim_world_clear(start_pos, point)) {
    return false;
  }

  return hitscan_aim_ray_hits_selected_area(player, hitbox_id, start_pos, point);
}

inline bool hitscan_aim_point_valid_for_player(Player* player, const aimbot_point& point) {
  if (!point.valid || player == nullptr || point.hitbox < 0 || !aimbot_vec3_is_finite(point.position)) {
    return false;
  }

  const Vec3 zero{};
  const bool point_is_zero = aimbot_distance_squared(point.position, zero) <= 0.25f;
  const bool player_near_zero = aimbot_distance_squared(player->get_origin(), zero) <= (128.0f * 128.0f);
  return !point_is_zero || player_near_zero;
}

inline aimbot_point hitscan_aim_find_bounds_point(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& bullet_view_angles,
  uint32_t hitbox_mask,
  bool require_visibility) {
  aimbot_point best_point{};
  const hitscan_aim_bounds bounds = hitscan_aim_get_player_bounds(player);
  if (localplayer == nullptr || player == nullptr || !bounds.valid) {
    return best_point;
  }

  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  constexpr int candidate_hitboxes[] = {
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

  for (int hitbox_id : candidate_hitboxes) {
    if (!aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask)) {
      continue;
    }

    const Vec3 point_position = hitscan_aim_bounds_point_for_hitbox(bounds, hitbox_id);
    if (!aimbot_vec3_is_finite(point_position)) {
      continue;
    }

    if (require_visibility && !hitscan_aim_point_visible_by_fallback(localplayer, player, hitbox_id, point_position)) {
      continue;
    }

    aimbot_point point{};
    point.valid = true;
    point.bone = 0;
    point.hitbox = hitbox_id;
    point.priority = aimbot_hitbox_priority(localplayer, player, weapon, hitbox_id);
    point.position = point_position;
    point.angles = aimbot_calculate_angles_to_position(shoot_pos, point_position);
    point.fov = aimbot_calculate_fov(point.angles, bullet_view_angles);

    if (!best_point.valid ||
        point.priority < best_point.priority ||
        (point.priority == best_point.priority && point.fov < best_point.fov)) {
      best_point = point;
    }
  }

  return best_point;
}

inline aimbot_point hitscan_aim_find_point(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& bullet_view_angles,
  bool require_visibility) {
  if (localplayer == nullptr || weapon == nullptr || player == nullptr) {
    return {};
  }

  const uint32_t configured_hitbox_mask = hitscan_aim_configured_hitbox_mask();
  const uint32_t hitbox_mask = configured_hitbox_mask;
  if (hitscan_aim_should_try_head_first(localplayer, weapon, hitbox_mask)) {
    aimbot_point head_point = aimbot_find_best_point(
      localplayer,
      player,
      weapon,
      bullet_view_angles,
      aim_hitbox_mask_head,
      require_visibility,
      aimbot_hitscan_trace_mask());
    if (hitscan_aim_point_valid_for_player(player, head_point)) {
      return head_point;
    }

    if (nographics::should_use_aimbot_trace_fallback()) {
      head_point = hitscan_aim_find_bounds_point(
        localplayer,
        weapon,
        player,
        bullet_view_angles,
        aim_hitbox_mask_head,
        require_visibility);
      if (head_point.valid) {
        return head_point;
      }
    }
  }

  aimbot_point point = aimbot_find_best_point(
    localplayer,
    player,
    weapon,
    bullet_view_angles,
    hitbox_mask,
    require_visibility,
    aimbot_hitscan_trace_mask());
  if (hitscan_aim_point_valid_for_player(player, point)) {
    return point;
  }

  if (!nographics::should_use_aimbot_trace_fallback()) {
    return {};
  }

  return hitscan_aim_find_bounds_point(
    localplayer,
    weapon,
    player,
    bullet_view_angles,
    hitbox_mask,
    require_visibility);
}

inline aimbot_candidate hitscan_aim_make_candidate(Player* localplayer,
  Player* player,
  const aimbot_point& point,
  const Vec3& original_view_angles,
  bool visible) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr || player == nullptr || !point.valid) {
    return candidate;
  }

  candidate.entity = player;
  candidate.player = player;
  candidate.preferred = has_aimbot_preference(player);
  candidate.bone = point.bone;
  candidate.hitbox = point.hitbox;
  candidate.aim_position = point.position;
  candidate.aim_angles = point.angles;
  candidate.fov = aimbot_calculate_fov(hitscan_aim_command_angles(localplayer, point.angles), original_view_angles);
  candidate.distance = distance_3d(localplayer->get_origin(), player->get_origin());
  candidate.health = player->get_health();
  candidate.simulation_time = player->get_simulation_time();
  candidate.tick_count = local_prediction_time_to_ticks(candidate.simulation_time + local_prediction_interp_time());
  candidate.command_angles = hitscan_aim_command_angles(localplayer, point.angles);
  candidate.visible = visible;
  return candidate;
}

inline bool hitscan_aim_textmode_candidate_visible(Player* localplayer, const aimbot_candidate& candidate);

inline aimbot_candidate hitscan_aim_find_candidate(Player* localplayer, Weapon* weapon, Player* player, const Vec3& original_view_angles) {
  if (localplayer == nullptr || weapon == nullptr || player == nullptr) return {};

  const Vec3 bullet_view_angles = hitscan_aim_bullet_angles(localplayer, original_view_angles);
  const aimbot_point point = hitscan_aim_find_point(localplayer, weapon, player, bullet_view_angles, true);
  return hitscan_aim_make_candidate(localplayer, player, point, original_view_angles, point.valid);
}

inline aimbot_candidate hitscan_aim_find_occluded_candidate(Player* localplayer, Weapon* weapon, Player* player, const Vec3& original_view_angles) {
  if (localplayer == nullptr || weapon == nullptr || player == nullptr) return {};

  const Vec3 bullet_view_angles = hitscan_aim_bullet_angles(localplayer, original_view_angles);
  const aimbot_point point = hitscan_aim_find_point(localplayer, weapon, player, bullet_view_angles, false);
  aimbot_candidate candidate = hitscan_aim_make_candidate(localplayer, player, point, original_view_angles, false);
  if (candidate.entity == nullptr) {
    return candidate;
  }

  candidate.visible = aimbot_trace_visible_to_position(localplayer, player, point.position, aimbot_hitscan_trace_mask());
  if (!candidate.visible) {
    candidate.visible = hitscan_aim_textmode_candidate_visible(localplayer, candidate);
  }

  return candidate;
}

inline bool hitscan_aim_textmode_candidate_visible(Player* localplayer, const aimbot_candidate& candidate) {
  if (!nographics::should_use_aimbot_trace_fallback() ||
      localplayer == nullptr ||
      candidate.player == nullptr ||
      !aimbot_vec3_is_finite(candidate.aim_position)) {
    return false;
  }

  const Vec3 start_pos = localplayer->get_shoot_pos();
  if (!aimbot_vec3_is_finite(start_pos) || !hitscan_aim_world_clear(start_pos, candidate.aim_position)) {
    return false;
  }

  return hitscan_aim_ray_hits_selected_area(
    candidate.player,
    candidate.hitbox,
    start_pos,
    candidate.aim_position);
}

inline bool hitscan_aim_trace_fallback(const aimbot_candidate& candidate,
  const Vec3& start_pos,
  const Vec3& end_pos) {
  if (candidate.entity == nullptr || !aimbot_vec3_is_finite(candidate.aim_position)) {
    return false;
  }

  if (!hitscan_aim_world_clear(start_pos, candidate.aim_position)) {
    return false;
  }

  if (candidate.player != nullptr) {
    if (candidate.hitbox < 0) {
      return false;
    }

    if (candidate.backtrack) {
      hitscan_aim_bounds bounds{};
      bounds.valid = true;
      bounds.mins = candidate.backtrack_mins;
      bounds.maxs = candidate.backtrack_maxs;
      return hitscan_aim_ray_hits_bounds_zone(
        hitscan_aim_bounds_zone_for_hitbox(bounds, candidate.hitbox),
        start_pos,
        end_pos);
    }

    return hitscan_aim_ray_hits_selected_area(candidate.player, candidate.hitbox, start_pos, end_pos);
  }

  return hitscan_aim_ray_hits_entity_bounds(candidate.entity, start_pos, end_pos);
}

inline bool hitscan_aim_textmode_trace_fallback(const aimbot_candidate& candidate,
  const Vec3& start_pos,
  const Vec3& end_pos) {
  if (!nographics::should_use_aimbot_trace_fallback() ||
      candidate.player == nullptr ||
      !aimbot_vec3_is_finite(candidate.aim_position)) {
    return false;
  }

  return hitscan_aim_trace_fallback(candidate, start_pos, end_pos);
}

inline bool hitscan_aim_trace_candidate(Player* localplayer,
  const aimbot_candidate& candidate,
  const Vec3& command_view_angles,
  const Vec3& spread_offset = {},
  bool use_spread = false,
  hitscan_aim_trace_result* result = nullptr) {
  if (result != nullptr) {
    *result = {};
  }

  if (localplayer == nullptr ||
      candidate.entity == nullptr ||
      engine_trace == nullptr ||
      (candidate.player != nullptr && candidate.hitbox < 0) ||
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

  const float target_distance = distance_3d(start_pos, candidate.aim_position);
  const float trace_distance = std::max(target_distance + 64.0f, 128.0f);
  Vec3 end_pos = start_pos + (forward * trace_distance);

  struct ray_t ray = engine_trace->init_ray(&start_pos, &end_pos);
  struct trace_filter filter;
  engine_trace->init_trace_filter(&filter, localplayer);

  struct trace_t trace_world{};
  engine_trace->trace_ray(&ray, aimbot_hitscan_trace_mask(), &filter, &trace_world);
  if (result != nullptr) {
    result->entity = static_cast<Entity*>(trace_world.entity);
    result->hitbox = trace_world.hitbox;
  }

  if (trace_world.entity != candidate.entity) {
    const bool fallback_hit = (candidate.backtrack || trace_world.entity == nullptr || nographics::should_use_aimbot_trace_fallback()) &&
      hitscan_aim_trace_fallback(candidate, start_pos, end_pos);
    if (result != nullptr) {
      result->hit = fallback_hit;
      if (fallback_hit) {
        result->entity = candidate.entity;
        result->hitbox = candidate.hitbox;
      }
    }
    return fallback_hit;
  }

  if (candidate.hitbox == aim_hitbox_head && candidate.player != nullptr) {
    const bool head_hit = trace_world.hitbox == aim_hitbox_head;
    if (result != nullptr) {
      result->hit = head_hit;
      if (head_hit) {
        result->entity = candidate.entity;
        result->hitbox = candidate.hitbox;
      }
    }
    return head_hit;
  }

  if (candidate.player != nullptr && candidate.hitbox >= 0 && trace_world.hitbox != candidate.hitbox) {
    const bool selected_hit = hitscan_aim_trace_fallback(candidate, start_pos, end_pos);
    if (result != nullptr) {
      result->hit = selected_hit;
      if (selected_hit) {
        result->entity = candidate.entity;
        result->hitbox = candidate.hitbox;
      }
    }
    return selected_hit;
  }

  if (result != nullptr) {
    result->hit = true;
  }
  return true;
}

#endif
