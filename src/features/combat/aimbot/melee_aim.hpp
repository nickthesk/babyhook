/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/combat/aimbot/melee_aim.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef MELEE_AIM_HPP
#define MELEE_AIM_HPP

#include <array>
#include <cmath>
#include <vector>

#include "aim_utils.hpp"
#include "core/entity_cache.hpp"
#include "features/movement/local_prediction/move_sim.hpp"

namespace melee_aim_detail {

constexpr float k_player_origin_compression = 0.125f;
constexpr float k_swing_time_cap = 0.35f;
constexpr float k_behind_dot_required = 0.0031f;
constexpr float k_facing_dot_required = 0.5f;
constexpr float k_facestab_dot_min = -0.3f;
constexpr float k_planar_min_distance = 0.30f;

inline bool is_knife(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }
  switch (weapon->get_def_id()) {
  case Spy_t_Knife:
  case Spy_t_KnifeR:
  case Spy_t_YourEternalReward:
  case Spy_t_ConniversKunai:
  case Spy_t_TheBigEarner:
  case Spy_t_TheWangaPrick:
  case Spy_t_TheSharpDresser:
  case Spy_t_TheSpycicle:
  case Spy_t_FestiveKnife:
  case Spy_t_TheBlackRose:
  case Spy_t_SilverBotkillerKnifeMkI:
  case Spy_t_GoldBotkillerKnifeMkI:
  case Spy_t_RustBotkillerKnifeMkI:
  case Spy_t_BloodBotkillerKnifeMkI:
  case Spy_t_CarbonadoBotkillerKnifeMkI:
  case Spy_t_DiamondBotkillerKnifeMkI:
  case Spy_t_SilverBotkillerKnifeMkII:
  case Spy_t_GoldBotkillerKnifeMkII:
    return true;
  default:
    return false;
  }
}

inline float compute_impact_time(Player* localplayer, Weapon* weapon) {
  if (weapon == nullptr || is_knife(weapon)) {
    return 0.0f;
  }
  const float current_time = global_vars != nullptr
    ? global_vars->curtime
    : (localplayer != nullptr ? localplayer->get_tickbase() * static_cast<float>(TICK_INTERVAL) : 0.0f);
  const float smack_time = weapon->get_smack_time();
  if (smack_time > current_time) {
    return std::clamp(smack_time - current_time, 0.0f, k_swing_time_cap);
  }
  return std::clamp(weapon->get_smack_delay(), 0.0f, k_swing_time_cap);
}

inline float effective_swing_time(Player* localplayer, Weapon* weapon) {
  float impact = compute_impact_time(localplayer, weapon);
  if (!config.aimbot.melee_swing_prediction) {
    impact = 0.0f;
  }
  const float extra = static_cast<float>(std::max(0, config.aimbot.melee_swing_extra_ticks)) *
    static_cast<float>(TICK_INTERVAL);
  return std::clamp(impact + extra, 0.0f, k_swing_time_cap + 0.2f);
}

inline Vec3 predict_local_swing_start(Player* localplayer, float swing_time) {
  if (localplayer == nullptr) {
    return Vec3{};
  }
  if (swing_time < 0.001f) {
    return localplayer->get_shoot_pos();
  }
  move_sim::path path = move_sim::predict_entity_path(localplayer, swing_time, false, false);
  if (!path.valid || path.positions.empty()) {
    return localplayer->get_shoot_pos();
  }
  return path.positions.back() + localplayer->get_view_offset();
}

inline Vec3 player_velocity(Player* player) {
  if (player == nullptr) {
    return Vec3{};
  }

  Vec3 velocity = local_prediction_estimate_entity_velocity(player);
  if (local_prediction_vector_length(velocity) <= 0.001f) {
    velocity = player->get_velocity();
  }
  return velocity;
}

inline Vec3 predict_local_swing_start_simple(Player* localplayer, float swing_time) {
  if (localplayer == nullptr) {
    return Vec3{};
  }
  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  if (swing_time < 0.001f) {
    return shoot_pos;
  }
  const float lead_time = std::min(swing_time, 0.10f);
  return shoot_pos + (player_velocity(localplayer) * lead_time);
}

inline Vec3 clamp_point_to_aabb(const Vec3& p, const Vec3& mins, const Vec3& maxs) {
  return Vec3{
    std::clamp(p.x, mins.x, maxs.x),
    std::clamp(p.y, mins.y, maxs.y),
    std::clamp(p.z, mins.z, maxs.z)
  };
}

inline Vec3 forward_xy(const Vec3& angles) {
  Vec3 f = local_prediction_angles_to_direction(angles);
  f.z = 0.0f;
  const float len = std::sqrt((f.x * f.x) + (f.y * f.y));
  if (len < 0.0001f) {
    return Vec3{1.0f, 0.0f, 0.0f};
  }
  return f * (1.0f / len);
}

inline bool razorback_blocks_backstab(Player* target) {
  if (target == nullptr || !config.aimbot.melee_ignore_razorback) {
    return false;
  }
  const auto& wearables = entity_cache_entities(class_id::WEARABLE_RAZORBACK);
  for (Entity* wearable : wearables) {
    if (wearable == nullptr) {
      continue;
    }
    if (wearable->get_owner_entity() == target) {
      return true;
    }
  }
  return false;
}

inline bool backstab_geometry_ok(Player* target,
  const Vec3& predicted_target_origin,
  const Vec3& eye_pos,
  const Vec3& aim_angles) {
  if (target == nullptr) {
    return false;
  }

  Vec3 to_target = predicted_target_origin - eye_pos;
  to_target.z = 0.0f;
  const float dist = std::sqrt((to_target.x * to_target.x) + (to_target.y * to_target.y));
  if (dist < k_planar_min_distance) {
    return false;
  }
  to_target = to_target * (1.0f / dist);

  const float comp_dist = k_player_origin_compression * 0.5f;
  const float extra = 2.0f * comp_dist / dist;
  const float pos_vs_target_min = k_behind_dot_required + extra;
  const float pos_vs_owner_min = k_facing_dot_required + extra;
  const float view_dot_min = k_facestab_dot_min + k_behind_dot_required;

  const Vec3 owner_forward = forward_xy(aim_angles);
  const Vec3 target_eye = target->get_eye_angles();
  const Vec3 target_forward = forward_xy(Vec3{0.0f, target_eye.y, 0.0f});

  const float pos_vs_target = (to_target.x * target_forward.x) + (to_target.y * target_forward.y);
  const float pos_vs_owner = (to_target.x * owner_forward.x) + (to_target.y * owner_forward.y);
  const float view_dot = (target_forward.x * owner_forward.x) + (target_forward.y * owner_forward.y);

  return pos_vs_target > pos_vs_target_min &&
    pos_vs_owner > pos_vs_owner_min &&
    view_dot > view_dot_min;
}

inline bool trace_hits_target(Player* localplayer,
  Entity* target,
  const Vec3& start,
  const Vec3& end,
  float hull) {
  if (engine_trace == nullptr) {
    return false;
  }

  Vec3 s = start;
  Vec3 e = end;
  Vec3 hull_mins = (hull > 0.0001f) ? Vec3{-hull, -hull, -hull} : Vec3{};
  Vec3 hull_maxs = (hull > 0.0001f) ? Vec3{hull, hull, hull} : Vec3{};

  ray_t ray = engine_trace->init_ray(&s, &e, &hull_mins, &hull_maxs);
  trace_filter filter{};
  engine_trace->init_trace_filter(&filter, localplayer);
  trace_t trace{};
  engine_trace->trace_ray(&ray, MASK_SOLID, &filter, &trace);

  if (trace.all_solid || trace.start_solid) {
    return false;
  }
  return trace.entity == target;
}

inline bool validate_reach(Player* localplayer,
  Weapon* weapon,
  Entity* target,
  const Vec3& predicted_target_origin,
  const Vec3& swing_start,
  const Vec3& aim_angles) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr || engine_trace == nullptr) {
    return false;
  }

  const float range = aimbot_get_melee_range(localplayer, weapon, target->get_class_id() == class_id::PLAYER ? static_cast<Player*>(target) : nullptr);
  const float hull = aimbot_get_melee_hull(localplayer, weapon, target->get_class_id() == class_id::PLAYER ? static_cast<Player*>(target) : nullptr);
  if (range <= 0.0f || hull < 0.0f) {
    return false;
  }

  Vec3 forward = local_prediction_angles_to_direction(aim_angles);
  if (!aimbot_vec3_is_finite(forward) || !aimbot_vec3_is_finite(swing_start)) {
    return false;
  }

  const Vec3 end = swing_start + (forward * range);

  // A quick AABB-pre-check so we don't bother tracing toward a target that's not on the segment.
  if (target->get_class_id() == class_id::PLAYER) {
    Player* tp = static_cast<Player*>(target);
    const Vec3 mins = tp->get_player_mins(tp->is_ducking());
    const Vec3 maxs = tp->get_player_maxs(tp->is_ducking());
    const Vec3 t_mins = mins + predicted_target_origin - Vec3{hull, hull, hull};
    const Vec3 t_maxs = maxs + predicted_target_origin + Vec3{hull, hull, hull};
    if (!aimbot_segment_intersects_aabb(swing_start, end, t_mins, t_maxs)) {
      return false;
    }
  }

  if (trace_hits_target(localplayer, target, swing_start, end, 0.0f)) {
    return true;
  }
  return trace_hits_target(localplayer, target, swing_start, end, hull);
}

struct swing_sample {
  bool valid = false;
  Vec3 origin{};
  Vec3 mins{};
  Vec3 maxs{};
  float at_time = 0.0f;
};

struct swing_sample_set {
  std::array<swing_sample, 2> samples{};
  size_t count = 0;
};

inline void add_swing_sample(swing_sample_set* out,
  Player* target,
  const Vec3& origin,
  const Vec3& mins,
  const Vec3& maxs,
  float at_time) {
  if (out == nullptr || target == nullptr || out->count >= out->samples.size()) {
    return;
  }

  swing_sample& sample = out->samples[out->count++];
  sample.valid = true;
  sample.origin = origin;
  sample.mins = mins;
  sample.maxs = maxs;
  sample.at_time = at_time;
}

inline swing_sample_set build_target_samples_simple(Player* target, float swing_time) {
  swing_sample_set out{};
  if (target == nullptr) {
    return out;
  }

  const Vec3 mins = target->get_player_mins(target->is_ducking());
  const Vec3 maxs = target->get_player_maxs(target->is_ducking());
  const Vec3 origin = target->get_origin();
  const float effective_horizon = swing_time > 0.001f
    ? std::max(swing_time, static_cast<float>(TICK_INTERVAL))
    : 0.0f;
  const Vec3 velocity = player_velocity(target);
  Vec3 sample_origin = origin;
  if (effective_horizon > 0.001f && local_prediction_vector_length(velocity) > 0.001f) {
    sample_origin = origin + (velocity * effective_horizon);
  }
  add_swing_sample(&out, target, sample_origin, mins, maxs, effective_horizon);

  return out;
}

inline std::vector<swing_sample> build_target_samples(Player* target, float swing_time) {
  std::vector<swing_sample> out;
  if (target == nullptr) {
    return out;
  }

  const float effective_horizon = swing_time > 0.001f
    ? std::max(swing_time, static_cast<float>(TICK_INTERVAL))
    : 0.0f;
  if (effective_horizon <= 0.001f) {
    swing_sample sample{};
    sample.valid = true;
    sample.origin = target->get_origin();
    sample.mins = target->get_player_mins(target->is_ducking());
    sample.maxs = target->get_player_maxs(target->is_ducking());
    sample.at_time = 0.0f;
    out.push_back(sample);
    return out;
  }

  move_sim::path path = move_sim::predict_entity_path(target, effective_horizon, false, config.aimbot.melee_account_ping);
  if (!path.valid || path.positions.empty()) {
    return out;
  }

  const Vec3 mins = target->get_player_mins(target->is_ducking());
  const Vec3 maxs = target->get_player_maxs(target->is_ducking());

  const size_t last = path.positions.size() - 1;
  swing_sample sample{};
  sample.valid = true;
  sample.origin = path.positions[last];
  sample.mins = mins;
  sample.maxs = maxs;
  sample.at_time = path.start_time + (static_cast<float>(last) * path.time_step);
  out.push_back(sample);

  return out;
}

struct attempt_result {
  bool valid = false;
  Vec3 aim_position{};
  Vec3 aim_angles{};
  float fov = FLT_MAX;
  float distance = FLT_MAX;
};

inline attempt_result try_aim_point(Player* localplayer,
  Weapon* weapon,
  Player* target,
  const swing_sample& sample,
  const Vec3& aim_position,
  const Vec3& swing_start,
  const Vec3& original_view_angles,
  bool require_backstab) {
  attempt_result r{};
  if (!aimbot_vec3_is_finite(aim_position)) {
    return r;
  }

  const Vec3 angles = aimbot_calculate_angles_to_position(swing_start, aim_position);
  if (!validate_reach(localplayer, weapon, target, sample.origin, swing_start, angles)) {
    return r;
  }
  if (require_backstab && !backstab_geometry_ok(target, sample.origin, swing_start, angles)) {
    return r;
  }

  r.valid = true;
  r.aim_position = aim_position;
  r.aim_angles = angles;
  r.fov = aimbot_calculate_fov(angles, original_view_angles);
  r.distance = distance_3d(swing_start, aim_position);
  return r;
}

}

inline float melee_aim_impact_time(Player* localplayer, Weapon* weapon) {
  return melee_aim_detail::compute_impact_time(localplayer, weapon);
}

inline Vec3 melee_aim_local_swing_start(Player* localplayer, float swing_time) {
  if (aimbot_simple_simulation_enabled()) {
    return melee_aim_detail::predict_local_swing_start_simple(localplayer, swing_time);
  }
  return melee_aim_detail::predict_local_swing_start(localplayer, swing_time);
}

inline bool melee_aim_trace_candidate(Player* localplayer,
  Weapon* weapon,
  Player* target,
  const Vec3& target_origin,
  const Vec3& swing_start,
  const Vec3& aim_angles) {
  if (!melee_aim_detail::validate_reach(localplayer, weapon, target, target_origin, swing_start, aim_angles)) {
    return false;
  }
  if (melee_aim_detail::is_knife(weapon) && config.aimbot.melee_auto_backstab) {
    if (melee_aim_detail::razorback_blocks_backstab(target)) {
      return false;
    }
    if (!melee_aim_detail::backstab_geometry_ok(target, target_origin, swing_start, aim_angles)) {
      return false;
    }
  }
  return true;
}

inline bool melee_aim_trace_candidate(Player* localplayer,
  Weapon* weapon,
  Player* target,
  const Vec3& target_origin,
  const Vec3& aim_angles) {
  if (localplayer == nullptr) {
    return false;
  }
  return melee_aim_trace_candidate(localplayer, weapon, target, target_origin, localplayer->get_shoot_pos(), aim_angles);
}

inline Vec3 melee_aim_lerp_vec3(const Vec3& start, const Vec3& end, float fraction) {
  return start + ((end - start) * fraction);
}

inline bool melee_aim_ready_candidate(Player* localplayer,
  Weapon* weapon,
  Player* target,
  const aimbot_candidate& candidate,
  const Vec3& aim_angles) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr || !candidate.melee_has_prediction) {
    return false;
  }

  const Vec3 current_swing_start = localplayer->get_shoot_pos();
  const Vec3 current_target_origin = target->get_origin();
  const bool predicted_ready = melee_aim_trace_candidate(
    localplayer,
    weapon,
    target,
    candidate.melee_target_origin,
    candidate.melee_swing_start,
    aim_angles);
  if (!predicted_ready) {
    return false;
  }

  if (melee_aim_trace_candidate(localplayer, weapon, target, current_target_origin, current_swing_start, aim_angles)) {
    return true;
  }

  const float impact_time = std::clamp(candidate.melee_impact_time, 0.0f, melee_aim_detail::k_swing_time_cap + 0.2f);
  if (impact_time <= static_cast<float>(TICK_INTERVAL)) {
    return false;
  }

  constexpr float ready_fraction = 0.55f;
  const Vec3 midway_swing_start = melee_aim_lerp_vec3(current_swing_start, candidate.melee_swing_start, ready_fraction);
  const Vec3 midway_target_origin = melee_aim_lerp_vec3(current_target_origin, candidate.melee_target_origin, ready_fraction);
  return melee_aim_trace_candidate(localplayer, weapon, target, midway_target_origin, midway_swing_start, aim_angles);
}

inline aimbot_candidate melee_aim_find_simple_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr || weapon == nullptr || player == nullptr) {
    return candidate;
  }

  const bool knife = melee_aim_detail::is_knife(weapon);
  const bool need_backstab = knife && config.aimbot.melee_auto_backstab;
  if (need_backstab && melee_aim_detail::razorback_blocks_backstab(player)) {
    return candidate;
  }

  const float swing_time = melee_aim_detail::effective_swing_time(localplayer, weapon);
  const Vec3 swing_start = melee_aim_detail::predict_local_swing_start_simple(localplayer, swing_time);
  if (!aimbot_vec3_is_finite(swing_start)) {
    return candidate;
  }

  const bool simple_bounds = config.aimbot.melee_nographics_simple_bounds;
  const melee_aim_detail::swing_sample_set samples = melee_aim_detail::build_target_samples_simple(player, swing_time);
  if (samples.count == 0) {
    return candidate;
  }

  aimbot_point hitbox_point{};
  if (!simple_bounds) {
    hitbox_point = aimbot_find_best_point(
      localplayer,
      player,
      weapon,
      original_view_angles,
      config.aimbot.melee_hitboxes,
      false);
  }

  melee_aim_detail::attempt_result best;
  const melee_aim_detail::swing_sample* best_sample = nullptr;

  for (size_t sample_index = 0; sample_index < samples.count; ++sample_index) {
    const melee_aim_detail::swing_sample& sample = samples.samples[sample_index];
    const Vec3 abs_mins = sample.origin + sample.mins;
    const Vec3 abs_maxs = sample.origin + sample.maxs;

    std::array<Vec3, 4> candidates{};
    int candidate_count = 0;

    if (need_backstab) {
      const float z = std::clamp(swing_start.z, abs_mins.z, abs_maxs.z);
      candidates[candidate_count++] = Vec3{sample.origin.x, sample.origin.y, z};
    }

    candidates[candidate_count++] = melee_aim_detail::clamp_point_to_aabb(swing_start, abs_mins, abs_maxs);

    if (!simple_bounds && hitbox_point.valid) {
      const Vec3 offset = hitbox_point.position - player->get_origin();
      candidates[candidate_count++] = sample.origin + offset;
    }

    candidates[candidate_count++] = (abs_mins + abs_maxs) * 0.5f;

    for (int i = 0; i < candidate_count; ++i) {
      melee_aim_detail::attempt_result attempt = melee_aim_detail::try_aim_point(
        localplayer,
        weapon,
        player,
        sample,
        candidates[i],
        swing_start,
        original_view_angles,
        need_backstab);
      if (!attempt.valid) {
        continue;
      }
      if (!best.valid || attempt.fov < best.fov) {
        best = attempt;
        best_sample = &sample;
      }
    }
  }

  if (!best.valid || best_sample == nullptr) {
    return candidate;
  }

  candidate.entity = player;
  candidate.player = player;
  candidate.preferred = aimbot::has_preference(player);
  candidate.bone = hitbox_point.valid ? hitbox_point.bone : aimbot_default_bone(localplayer, player, weapon);
  candidate.hitbox = hitbox_point.valid ? hitbox_point.hitbox : aim_hitbox_pelvis;
  candidate.studio_hitbox = hitbox_point.valid ? hitbox_point.studio_hitbox : -1;
  candidate.aim_position = best.aim_position;
  candidate.aim_angles = best.aim_angles;
  candidate.command_angles = best.aim_angles;
  candidate.fov = best.fov;
  candidate.distance = best.distance;
  candidate.health = player->get_health();
  candidate.visible = true;
  candidate.melee_has_prediction = true;
  candidate.melee_impact_time = best_sample->at_time;
  candidate.melee_swing_start = swing_start;
  candidate.melee_target_origin = best_sample->origin;
  candidate.simulation_time = player->get_simulation_time();
  return candidate;
}

inline aimbot_candidate melee_aim_find_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  user_cmd*,
  const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr || weapon == nullptr || player == nullptr) {
    return candidate;
  }

  if (aimbot_simple_simulation_enabled()) {
    return melee_aim_find_simple_candidate(localplayer, weapon, player, original_view_angles);
  }

  const bool knife = melee_aim_detail::is_knife(weapon);
  const bool need_backstab = knife && config.aimbot.melee_auto_backstab;
  if (need_backstab && melee_aim_detail::razorback_blocks_backstab(player)) {
    return candidate;
  }

  const float swing_time = melee_aim_detail::effective_swing_time(localplayer, weapon);
  const Vec3 swing_start = melee_aim_detail::predict_local_swing_start(localplayer, swing_time);
  if (!aimbot_vec3_is_finite(swing_start)) {
    return candidate;
  }

  std::vector<melee_aim_detail::swing_sample> samples = melee_aim_detail::build_target_samples(player, swing_time);
  if (samples.empty()) {
    melee_aim_detail::swing_sample fallback;
    fallback.valid = true;
    fallback.origin = player->get_origin();
    fallback.mins = player->get_player_mins(player->is_ducking());
    fallback.maxs = player->get_player_maxs(player->is_ducking());
    fallback.at_time = 0.0f;
    samples.push_back(fallback);
  }

  aimbot_point hitbox_point = aimbot_find_best_point(
    localplayer,
    player,
    weapon,
    original_view_angles,
    config.aimbot.melee_hitboxes,
    false);

  melee_aim_detail::attempt_result best;
  const melee_aim_detail::swing_sample* best_sample = nullptr;

  for (const auto& sample : samples) {
    const Vec3 abs_mins = sample.origin + sample.mins;
    const Vec3 abs_maxs = sample.origin + sample.maxs;

    std::array<Vec3, 4> candidates{};
    int candidate_count = 0;

    if (need_backstab) {
      // For backstab: aim at the origin column at our eye height so the trace clears
      // and we end up planted right behind the spy's target.
      const float z = std::clamp(swing_start.z, abs_mins.z, abs_maxs.z);
      candidates[candidate_count++] = Vec3{sample.origin.x, sample.origin.y, z};
    }

    candidates[candidate_count++] = melee_aim_detail::clamp_point_to_aabb(swing_start, abs_mins, abs_maxs);

    if (hitbox_point.valid) {
      const Vec3 offset = hitbox_point.position - player->get_origin();
      candidates[candidate_count++] = sample.origin + offset;
    }

    candidates[candidate_count++] = (abs_mins + abs_maxs) * 0.5f;

    for (int i = 0; i < candidate_count; ++i) {
      melee_aim_detail::attempt_result attempt = melee_aim_detail::try_aim_point(
        localplayer,
        weapon,
        player,
        sample,
        candidates[i],
        swing_start,
        original_view_angles,
        need_backstab);
      if (!attempt.valid) {
        continue;
      }
      if (!best.valid || attempt.fov < best.fov) {
        best = attempt;
        best_sample = &sample;
      }
    }
  }

  if (!best.valid || best_sample == nullptr) {
    return candidate;
  }

  candidate.entity = player;
  candidate.player = player;
  candidate.preferred = aimbot::has_preference(player);
  candidate.bone = hitbox_point.valid ? hitbox_point.bone : aimbot_default_bone(localplayer, player, weapon);
  candidate.hitbox = hitbox_point.valid ? hitbox_point.hitbox : aim_hitbox_pelvis;
  candidate.studio_hitbox = hitbox_point.valid ? hitbox_point.studio_hitbox : -1;
  candidate.aim_position = best.aim_position;
  candidate.aim_angles = best.aim_angles;
  candidate.command_angles = best.aim_angles;
  candidate.fov = best.fov;
  candidate.distance = best.distance;
  candidate.health = player->get_health();
  candidate.visible = true;
  candidate.melee_has_prediction = true;
  candidate.melee_impact_time = best_sample->at_time;
  candidate.melee_swing_start = swing_start;
  candidate.melee_target_origin = best_sample->origin;
  candidate.simulation_time = player->get_simulation_time();
  return candidate;
}

#endif
