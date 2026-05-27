#ifndef AIM_TARGETING_HPP
#define AIM_TARGETING_HPP

#include <algorithm>
#include <cstddef>
#include <vector>

#include "aim_state.hpp"
#include "aim_spread.hpp"
#include "aim_utils.hpp"
#include "aimbot.hpp"
#include "hitscan_aim.hpp"
#include "melee_aim.hpp"
#include "proj_aim.hpp"

#include "core/entity_cache.hpp"

#include "features/combat/backtrack/backtrack.hpp"
#include "features/combat/aimbot/projectile/projectile_sim.hpp"

namespace aim_targeting {

inline aimbot_candidate find_hitscan_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& original_view_angles,
  bool relaxed_selection) {
  aimbot_candidate candidate = hitscan_aim_find_candidate(localplayer, weapon, player, original_view_angles);
  if (candidate.entity != nullptr || !relaxed_selection) {
    return candidate;
  }

  candidate = hitscan_aim_find_occluded_candidate(localplayer, weapon, player, original_view_angles);
  if (!candidate.visible) {
    return {};
  }

  return candidate;
}

inline void consider_non_player_target(Player* localplayer,
  Weapon* weapon,
  Entity* entity,
  const Vec3& original_view_angles,
  aimbot_candidate* best_candidate) {
  if (best_candidate == nullptr) {
    return;
  }

  aimbot_candidate candidate = aimbot_find_non_player_candidate(localplayer, weapon, entity, original_view_angles);
  if (candidate.entity == nullptr) {
    return;
  }

  if (!candidate.visible || !aimbot_fov_within_limit(candidate.fov)) {
    return;
  }

  if (aimbot_candidate_better(candidate, *best_candidate)) {
    *best_candidate = candidate;
  }
}

inline aimbot_candidate find_best_non_player_candidate(Player* localplayer, Weapon* weapon, const Vec3& original_view_angles) {
  aimbot_candidate best_candidate{};

  constexpr class_id building_ids[] = {
    class_id::SENTRY,
    class_id::DISPENSER,
    class_id::TELEPORTER
  };

  for (class_id building_id : building_ids) {
    for (Entity* entity : entity_cache[building_id]) {
      consider_non_player_target(localplayer, weapon, entity, original_view_angles, &best_candidate);
    }
  }

  for (Entity* entity : entity_cache[class_id::PUMPKIN]) {
    consider_non_player_target(localplayer, weapon, entity, original_view_angles, &best_candidate);
  }

  for (Entity* entity : entity_cache[class_id::PILL_OR_STICKY]) {
    consider_non_player_target(localplayer, weapon, entity, original_view_angles, &best_candidate);
  }

  return best_candidate;
}

struct projectile_target_hint {
  Player* player = nullptr;
  float fov = FLT_MAX;
  float current_fov = FLT_MAX;
  float lead_fov = FLT_MAX;
  float distance = FLT_MAX;
  float target_speed = 0.0f;
  int health = 0;
  bool preferred = false;
  bool current = false;
};

inline Vec3 projectile_hint_position(Player* localplayer, Weapon* weapon, Player* player) {
  if (player == nullptr) {
    return Vec3{};
  }

  const Vec3 origin = player->get_origin();
  const Vec3 mins = player->get_player_mins(player->is_ducking());
  const Vec3 maxs = player->get_player_maxs(player->is_ducking());
  if (proj_aim_is_direct_hit_weapon(weapon) && player->is_on_ground()) {
    return origin + Vec3{0.0f, 0.0f, mins.z + 6.0f};
  }

  const uint32_t hitbox_mask = proj_aim_effective_hitbox_mask(localplayer, weapon, player);
  const std::vector<proj_aim_hitbox_sample> hitbox_samples = proj_aim_hitbox_samples(player, hitbox_mask);
  if (!hitbox_samples.empty()) {
    const proj_aim_hitbox_sample* best_sample = &hitbox_samples.front();
    for (const proj_aim_hitbox_sample& sample : hitbox_samples) {
      if (sample.priority < best_sample->priority) {
        best_sample = &sample;
      }
    }
    return origin + best_sample->offset;
  }

  return origin + Vec3{0.0f, 0.0f, mins.z + ((maxs.z - mins.z) * 0.5f)};
}

inline int projectile_target_attempt_cap(size_t target_count) {
  int cap = 5;
  const float frame_time = aim_state::actual_frame_time();
  if (frame_time > (1.0f / 45.0f)) cap = 2;
  else if (frame_time > (1.0f / 60.0f)) cap = 3;
  else if (frame_time > (1.0f / 75.0f)) cap = 4;

  if (target_count >= 8) cap = std::min(cap, 3);
  else if (target_count >= 6) cap = std::min(cap, 4);

  const int user_cap = std::clamp(config.aimbot.projectile_max_targets, 1, 12);
  cap = std::min(cap, user_cap);
  return std::clamp(cap, 1, 12);
}

inline bool projectile_target_hint_better(const projectile_target_hint& left, const projectile_target_hint& right) {
  if (left.current != right.current) return left.current;
  if (left.preferred != right.preferred) return left.preferred;

  switch (config.aimbot.target_type) {
  case Aim::TargetType::DISTANCE:
    if (left.distance != right.distance) return left.distance < right.distance;
    return left.fov < right.fov;
  case Aim::TargetType::LEAST_HEALTH:
    if (left.health != right.health) return left.health < right.health;
    return left.fov < right.fov;
  case Aim::TargetType::MOST_HEALTH:
    if (left.health != right.health) return left.health > right.health;
    return left.fov < right.fov;
  case Aim::TargetType::FOV:
  default:
    if (left.fov != right.fov) return left.fov < right.fov;
    return left.distance < right.distance;
  }
}

inline aimbot_candidate find_best_projectile_candidate(Player* localplayer,
  Weapon* weapon,
  user_cmd* user_cmd,
  const Vec3& original_view_angles) {
  aimbot_candidate best_candidate{};
  std::vector<projectile_target_hint> target_hints{};
  target_hints.reserve(entity_cache_players().size());

  const projectile_sim_profile profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  const float projectile_speed = profile.valid ? std::max(profile.params.speed, 1.0f) : 1.0f;
  const float projectile_horizon = profile.valid ? std::max(profile.params.max_time, static_cast<float>(TICK_INTERVAL)) : 1.0f;
  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    Player* player = entry.player;
    ++aim_state::scan.candidates_total;
    const auto skip_reason = aimbot_player_skip_reason_for(localplayer, player);
    if (skip_reason != aimbot_player_skip_reason::none) {
      aim_state::record_player_skip(skip_reason);
      continue;
    }

    const Vec3 aim_pos = projectile_hint_position(localplayer, weapon, player);
    Vec3 target_velocity = local_prediction_estimate_entity_velocity(player);
    if (local_prediction_vector_length(target_velocity) <= 0.001f) {
      target_velocity = player->get_velocity();
    }

    const float distance = distance_3d(shoot_pos, player->get_origin());
    const float approximate_time = std::clamp(distance / projectile_speed, static_cast<float>(TICK_INTERVAL), projectile_horizon);
    const Vec3 lead_pos = aim_pos + (target_velocity * approximate_time);
    const Vec3 aim_angles = aimbot_calculate_angles_to_position(shoot_pos, aim_pos);
    const Vec3 lead_angles = aimbot_calculate_angles_to_position(shoot_pos, lead_pos);
    const bool preferred = aimbot::has_preference(player);
    const bool current = player == aimbot::active_target_player() || preferred;
    const float current_fov = aimbot_calculate_fov(aim_angles, original_view_angles);
    const float lead_fov = aimbot_calculate_fov(lead_angles, original_view_angles);
    const float fov = std::min(current_fov, lead_fov);
    const float target_speed = local_prediction_velocity_2d_length(target_velocity);
    const float speed_lead_fov = std::atan2(target_speed * approximate_time, std::max(distance, 1.0f)) * radpi;
    const float fov_limit = aimbot_fov_limit(
      current ? 3.5f : 2.75f,
      current ? 90.0f : 64.0f,
      speed_lead_fov * 0.75f);
    if (!current && fov > fov_limit) {
      ++aim_state::scan.candidates_rejected;
      continue;
    }

    ++aim_state::scan.candidates_visible;
    target_hints.push_back({
      .player = player,
      .fov = fov,
      .current_fov = current_fov,
      .lead_fov = lead_fov,
      .distance = distance,
      .target_speed = target_speed,
      .health = player->get_health(),
      .preferred = preferred,
      .current = current
    });
  }

  if (target_hints.empty()) {
    proj_aim_set_scan_debug_stats(0, 0, 0);
    return best_candidate;
  }

  const int max_attempts = projectile_target_attempt_cap(target_hints.size());
  const size_t sort_cap = std::min(target_hints.size(), static_cast<size_t>(max_attempts));
  if (target_hints.size() > sort_cap) {
    std::partial_sort(
      target_hints.begin(),
      target_hints.begin() + static_cast<std::ptrdiff_t>(sort_cap),
      target_hints.end(),
      projectile_target_hint_better);
  } else {
    std::stable_sort(target_hints.begin(), target_hints.end(), projectile_target_hint_better);
  }

  int attempts = 0;
  for (const projectile_target_hint& hint : target_hints) {
    if (attempts >= max_attempts) break;
    ++attempts;

    aimbot_candidate candidate = proj_aim_find_candidate(localplayer, weapon, hint.player, user_cmd, original_view_angles);
    if (candidate.player == nullptr) {
      ++aim_state::scan.candidates_rejected;
      continue;
    }

    if (!candidate.visible || !aimbot_fov_within_limit(candidate.fov, candidate.preferred ? 1.35f : 1.0f)) {
      ++aim_state::scan.candidates_rejected;
      continue;
    }

    if (aimbot_candidate_better(candidate, best_candidate)) {
      best_candidate = candidate;
      proj_aim_commit_debug_stats();
    }
  }

  proj_aim_set_scan_debug_stats(static_cast<int>(target_hints.size()), attempts, max_attempts);
  return best_candidate;
}

inline aimbot_candidate find_best_candidate(Player* localplayer, Weapon* weapon, user_cmd* user_cmd, const Vec3& original_view_angles) {
  aimbot_candidate best_candidate{};
  aimbot_candidate best_ready_hitscan_candidate{};
  aim_state::scan = {};

  if (aimbot_is_projectile_weapon(weapon)) {
    best_candidate = find_best_projectile_candidate(localplayer, weapon, user_cmd, original_view_angles);
  } else {
    const bool hitscan_ready_selection = !aimbot_is_melee_weapon(weapon);
    for (const entity_cache_player_entry& entry : entity_cache_players()) {
      Player* player = entry.player;
      ++aim_state::scan.candidates_total;
      const auto skip_reason = aimbot_player_skip_reason_for(localplayer, player);
      if (skip_reason != aimbot_player_skip_reason::none) {
        aim_state::record_player_skip(skip_reason);
        continue;
      }

      aimbot_candidate candidate{};
      if (aimbot_is_melee_weapon(weapon)) {
        candidate = melee_aim_find_candidate(localplayer, weapon, player, user_cmd, original_view_angles);
      } else {
        candidate = find_hitscan_candidate(localplayer, weapon, player, original_view_angles, false);
        const aimbot_candidate backtrack_candidate = backtrack::find_hitscan_candidate(
          localplayer,
          weapon,
          player,
          original_view_angles,
          aimbot::has_preference(player));
        if (aimbot_candidate_better(backtrack_candidate, candidate)) {
          candidate = backtrack_candidate;
        }
      }

      if (candidate.entity == nullptr) {
        ++aim_state::scan.candidates_rejected;
        continue;
      }

      if (candidate.visible) {
        ++aim_state::scan.candidates_visible;
      }

      if (!candidate.visible || !aimbot_fov_within_limit(candidate.fov, candidate.preferred ? 1.35f : 1.0f)) {
        ++aim_state::scan.candidates_rejected;
        continue;
      }

      if (aimbot_candidate_better(candidate, best_candidate)) {
        best_candidate = candidate;
      }

      if (hitscan_ready_selection &&
          aim_spread::hitscan_candidate_ready_for_selection(localplayer, weapon, user_cmd, candidate) &&
          aimbot_candidate_better(candidate, best_ready_hitscan_candidate)) {
        best_ready_hitscan_candidate = candidate;
      }
    }
  }

  const aimbot_candidate non_player_candidate = find_best_non_player_candidate(localplayer, weapon, original_view_angles);
  if (aimbot_candidate_better(non_player_candidate, best_candidate)) {
    best_candidate = non_player_candidate;
  }

  if (best_ready_hitscan_candidate.entity != nullptr &&
      !aim_spread::hitscan_candidate_ready_for_selection(localplayer, weapon, user_cmd, best_candidate)) {
    best_candidate = best_ready_hitscan_candidate;
  }

  return best_candidate;
}

inline aimbot_candidate find_best_scope_candidate(Player* localplayer, Weapon* weapon, const Vec3& original_view_angles) {
  aimbot_candidate best_candidate{};
  if (localplayer == nullptr || weapon == nullptr || localplayer->get_tf_class() != tf_class::SNIPER || !weapon->is_sniper_rifle()) {
    return best_candidate;
  }

  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    Player* player = entry.player;
    ++aim_state::scan.candidates_total;
    const auto skip_reason = aimbot_player_skip_reason_for(localplayer, player);
    if (skip_reason != aimbot_player_skip_reason::none) {
      aim_state::record_player_skip(skip_reason);
      continue;
    }

    aimbot_candidate candidate = hitscan_aim_find_occluded_candidate(localplayer, weapon, player, original_view_angles);
    if (candidate.player == nullptr) continue;
    if (!aimbot_fov_within_limit(candidate.fov, candidate.preferred ? 1.35f : 1.0f)) continue;
    if (aimbot_candidate_better(candidate, best_candidate)) {
      best_candidate = candidate;
    }
  }

  return best_candidate;
}

inline bool projectile_solution_ready(Player* localplayer,
  Weapon* weapon,
  user_cmd* user_cmd,
  const aimbot_candidate& candidate,
  const Vec3& applied_view_angles) {
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr || candidate.entity == nullptr) return false;
  if (!candidate.projectile_direct && !candidate.projectile_splash) return true;
  if (candidate.player == nullptr) return false;

  projectile_sim_profile profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  if (!profile.valid || profile.params.speed <= 0.0f || candidate.projectile_intercept_time <= 0.0f) return false;

  LocalPredictionInterceptResult adjusted_intercept{};
  adjusted_intercept.valid = true;
  adjusted_intercept.has_target_base_origin = candidate.projectile_has_target_base_origin;
  adjusted_intercept.intercept_time = candidate.projectile_intercept_time;
  adjusted_intercept.intercept_distance = candidate.distance;
  adjusted_intercept.miss_distance = candidate.projectile_miss_distance;
  adjusted_intercept.aim_angles = applied_view_angles;
  adjusted_intercept.target_origin = candidate.aim_position;
  adjusted_intercept.target_base_origin = candidate.projectile_target_base_origin;
  adjusted_intercept.target_offset = candidate.projectile_target_offset;

  if (candidate.projectile_direct) {
    return proj_aim_trace_simple_path(localplayer, candidate.player, weapon, adjusted_intercept);
  }

  const uint32_t hitbox_mask = proj_aim_effective_hitbox_mask(localplayer, weapon, candidate.player);
  const Vec3* predicted_target_origin = candidate.projectile_has_target_base_origin
    ? &candidate.projectile_target_base_origin
    : nullptr;
  return proj_aim_trace_splash_path(
    localplayer,
    candidate.player,
    weapon,
    adjusted_intercept,
    candidate.projectile_splash_radius,
    hitbox_mask,
    nullptr,
    predicted_target_origin);
}

}

#endif
