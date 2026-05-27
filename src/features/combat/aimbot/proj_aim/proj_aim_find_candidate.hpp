/*
/^-----^\   data: 2026-05-15
V  o o  V  file: src/features/combat/aimbot/proj_aim/proj_aim_find_candidate.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef PROJ_AIM_FIND_CANDIDATE_HPP
#define PROJ_AIM_FIND_CANDIDATE_HPP

#include <algorithm>
#include <array>
#include <cstddef>

#include "proj_aim_budget.hpp"
#include "proj_aim_splash.hpp"

inline Vec3 proj_aim_simple_target_velocity(Player* player) {
  if (player == nullptr) {
    return Vec3{};
  }

  Vec3 target_velocity = local_prediction_estimate_entity_velocity(player);
  if (local_prediction_vector_length(target_velocity) <= 0.001f) {
    target_velocity = player->get_velocity();
  }
  return target_velocity;
}

inline size_t proj_aim_simple_direct_points(Player* localplayer,
  Weapon* weapon,
  Player* target,
  uint32_t hitbox_mask,
  std::array<proj_aim_direct_point, 3>& points) {
  if (target == nullptr) {
    return 0;
  }

  const Vec3 mins = target->get_player_mins(target->is_ducking());
  const Vec3 maxs = target->get_player_maxs(target->is_ducking());
  const float height = std::max(maxs.z - mins.z, 1.0f);
  size_t point_count = 0;
  const auto add_point = [&](int hitbox, float height_fraction) {
    if (point_count >= points.size()) {
      return;
    }
    points[point_count++] = {
      .hitbox = hitbox,
      .bone = 0,
      .priority = aimbot_hitbox_priority(localplayer, target, weapon, hitbox),
      .offset = Vec3{0.0f, 0.0f, mins.z + (height * height_fraction)}
    };
  };

  if (aimbot_hitbox_matches_mask(aim_hitbox_spine_1, hitbox_mask)) {
    add_point(aim_hitbox_spine_1, 0.55f);
  }
  if (aimbot_hitbox_matches_mask(aim_hitbox_pelvis, hitbox_mask)) {
    add_point(aim_hitbox_pelvis, 0.36f);
  }
  if (aimbot_hitbox_matches_mask(aim_hitbox_head, hitbox_mask)) {
    add_point(aim_hitbox_head, 0.86f);
  }
  if (point_count == 0) {
    add_point(aim_hitbox_spine_1, 0.55f);
  }

  std::sort(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(point_count), [](const auto& left, const auto& right) {
    return left.priority < right.priority;
  });
  return point_count;
}

inline LocalPredictionEntityPath proj_aim_simple_debug_path(Player* target,
  const Vec3& base_origin,
  const Vec3& target_velocity,
  float horizon_seconds) {
  LocalPredictionEntityPath path{};
  if (target == nullptr) {
    return path;
  }

  path.valid = true;
  path.used_movement_sim = false;
  path.used_game_engine_movement = false;
  path.used_strafe_prediction = false;
  path.start_time = 0.0f;
  path.time_step = std::max(horizon_seconds, static_cast<float>(TICK_INTERVAL));
  path.final_velocity = target_velocity;
  path.positions.reserve(2);
  path.positions.emplace_back(base_origin);
  path.positions.emplace_back(base_origin + (target_velocity * path.time_step));
  return path;
}

inline LocalPredictionInterceptResult proj_aim_simple_projectile_intercept(Player* localplayer,
  Weapon* weapon,
  const proj_aim_weapon_profile& weapon_profile,
  const Vec3& target_base_origin,
  const Vec3& target_velocity,
  const Vec3& target_offset) {
  LocalPredictionInterceptResult result{};
  if (localplayer == nullptr || weapon == nullptr || weapon_profile.params.speed <= 0.0f) {
    return result;
  }

  projectile_sim_profile sim_profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  if (!sim_profile.valid || sim_profile.params.speed <= 0.0f) {
    return result;
  }

  sim_profile.params.max_time = std::min(sim_profile.params.max_time, weapon_profile.params.max_time);
  sim_profile.lifetime = sim_profile.params.max_time;

  const Vec3 initial_target = target_base_origin + target_offset;
  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  float estimate_time = std::clamp(
    distance_3d(shoot_pos, initial_target) / std::max(sim_profile.params.speed, 1.0f),
    sim_profile.params.time_step,
    sim_profile.params.max_time);

  float best_score = FLT_MAX;
  float best_time = 0.0f;
  float best_miss = FLT_MAX;
  float best_distance = FLT_MAX;
  Vec3 best_target_base{};
  Vec3 best_target{};
  projectile_sim_launch best_launch{};

  constexpr int iteration_count = 3;
  for (int iteration = 0; iteration < iteration_count; ++iteration) {
    const Vec3 predicted_base = target_base_origin + (target_velocity * estimate_time);
    const Vec3 predicted_target = predicted_base + target_offset;
    float iteration_score = FLT_MAX;
    float iteration_time = estimate_time;
    projectile_sim_launch iteration_launch{};

    const int arc_branch_count = local_prediction_projectile_arc_branch_count(sim_profile.params.gravity > 0.0f);
    for (int arc_branch = 0; arc_branch < arc_branch_count; ++arc_branch) {
      const bool high_arc = local_prediction_projectile_arc_high_branch(sim_profile.params.gravity > 0.0f, arc_branch);
      projectile_sim_launch candidate_launch{};
      float flight_time = 0.0f;
      if (!projectile_sim_solve_launch_to_point(
          localplayer,
          weapon,
          sim_profile,
          predicted_target,
          high_arc,
          &candidate_launch,
          &flight_time)) {
        continue;
      }

      if (!candidate_launch.valid || flight_time <= 0.0f || flight_time > sim_profile.params.max_time) {
        continue;
      }

      const Vec3 final_base = target_base_origin + (target_velocity * flight_time);
      const Vec3 final_target = final_base + target_offset;
      const float spatial_error = distance_3d(projectile_sim_position_at_time(candidate_launch, sim_profile, flight_time), final_target);
      const float time_error = std::fabs(flight_time - estimate_time);
      const float arc_bias = local_prediction_projectile_arc_score_bias(sim_profile, high_arc, flight_time);
      const float score = spatial_error + (time_error * sim_profile.params.speed) + arc_bias;
      if (score < iteration_score) {
        iteration_score = score;
        iteration_time = flight_time;
        iteration_launch = candidate_launch;
      }
      if (score < best_score) {
        best_score = score;
        best_time = flight_time;
        best_miss = spatial_error;
        best_distance = distance_3d(shoot_pos, final_target);
        best_target_base = final_base;
        best_target = final_target;
        best_launch = candidate_launch;
      }
    }

    if (!iteration_launch.valid) {
      break;
    }
    estimate_time = iteration_time;
  }

  constexpr float player_hull_extra = 24.0f;
  if (!best_launch.valid || best_miss > projectile_sim_direct_tolerance(sim_profile) + player_hull_extra) {
    return {};
  }

  result.valid = true;
  result.has_target_base_origin = true;
  result.intercept_time = best_time;
  result.intercept_distance = best_distance;
  result.miss_distance = best_miss;
  result.aim_angles = best_launch.angles;
  result.target_origin = best_target;
  result.target_base_origin = best_target_base;
  result.target_offset = target_offset;
  result.target_velocity = target_velocity;
  result.trace.valid = true;
  result.trace.steps.reserve(3);
  result.trace.steps.emplace_back(LocalPredictionProjectileStep{
    .time = 0.0f,
    .position = best_launch.origin,
    .velocity = projectile_sim_initial_velocity(best_launch, sim_profile)
  });
  if (best_time * 0.5f > sim_profile.params.time_step) {
    result.trace.steps.emplace_back(LocalPredictionProjectileStep{
      .time = best_time * 0.5f,
      .position = projectile_sim_position_at_time(best_launch, sim_profile, best_time * 0.5f),
      .velocity = Vec3{}
    });
  }
  result.trace.steps.emplace_back(LocalPredictionProjectileStep{
    .time = best_time,
    .position = projectile_sim_position_at_time(best_launch, sim_profile, best_time),
    .velocity = Vec3{}
  });
  return result;
}

inline aimbot_candidate proj_aim_find_simple_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  user_cmd*,
  const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr || weapon == nullptr || player == nullptr) {
    return candidate;
  }

  const proj_aim_weapon_profile profile = proj_aim_profile_for_weapon(weapon);
  if (profile.params.speed <= 0.0f) {
    return candidate;
  }

  proj_aim_budget_guard budget_scope{};
  proj_aim_budget_begin_for_distance(distance_3d(localplayer->get_shoot_pos(), player->get_origin()));

  const Vec3 target_velocity = proj_aim_simple_target_velocity(player);
  const projectile_timing_context timing_context = proj_aim_build_timing_context(player);
  const Vec3 target_base_origin = player->get_origin() + (target_velocity * timing_context.clamped_lead_time);
  const uint32_t configured_hitbox_mask = config.aimbot.projectile_hitboxes;
  const uint32_t hitbox_mask = proj_aim_effective_hitbox_mask(localplayer, weapon, player);
  std::array<proj_aim_direct_point, 3> direct_points{};
  const size_t direct_point_count = proj_aim_simple_direct_points(localplayer, weapon, player, hitbox_mask, direct_points);

  LocalPredictionEntityPath debug_path{};
  if (config.aimbot.projectile_debug) {
    debug_path = proj_aim_simple_debug_path(player, target_base_origin, target_velocity, profile.params.max_time);
    proj_aim_reset_debug_stats(weapon, player, debug_path, configured_hitbox_mask, hitbox_mask);
    proj_aim_current_debug_stats.direct_points = static_cast<int>(direct_point_count);
    proj_aim_last_direct_history.clear();
    proj_aim_last_splash_history.clear();
  }

  aimbot_candidate direct_candidate{};
  int direct_candidate_priority = INT_MAX;
  for (size_t point_index = 0; point_index < direct_point_count; ++point_index) {
    const proj_aim_direct_point& sample = direct_points[point_index];
    if (config.aimbot.projectile_debug) {
      ++proj_aim_current_debug_stats.direct_solves;
    }

    LocalPredictionInterceptResult intercept = proj_aim_simple_projectile_intercept(
      localplayer,
      weapon,
      profile,
      target_base_origin,
      target_velocity,
      sample.offset);
    if (!intercept.valid || intercept.intercept_time < profile.arm_time) {
      continue;
    }
    if (config.aimbot.projectile_debug) {
      ++proj_aim_current_debug_stats.direct_intercepts;
    }

    const float direct_fov = aimbot_calculate_fov(intercept.aim_angles, original_view_angles);
    if (!aimbot_fov_within_limit(direct_fov, 1.2f, 3.0f)) {
      continue;
    }

    if (!proj_aim_trace_simple_path(localplayer, player, weapon, intercept)) {
      if (config.aimbot.projectile_debug) {
        ++proj_aim_current_debug_stats.direct_trace_rejects;
      }
      continue;
    }

    aimbot_candidate point_candidate{};
    point_candidate.entity = player;
    point_candidate.player = player;
    point_candidate.preferred = aimbot::has_preference(player);
    point_candidate.bone = sample.bone;
    point_candidate.hitbox = sample.hitbox;
    point_candidate.aim_position = intercept.target_origin;
    point_candidate.aim_angles = intercept.aim_angles;
    point_candidate.fov = direct_fov;
    point_candidate.distance = intercept.intercept_distance;
    point_candidate.health = player->get_health();
    point_candidate.visible = true;
    point_candidate.projectile_direct = true;
    point_candidate.projectile_has_target_base_origin = intercept.has_target_base_origin;
    point_candidate.projectile_intercept_time = intercept.intercept_time;
    point_candidate.projectile_miss_distance = intercept.miss_distance;
    point_candidate.projectile_target_base_origin = intercept.target_base_origin;
    point_candidate.projectile_target_offset = intercept.target_offset;

    const int point_priority = sample.priority;
    if (direct_candidate.entity == nullptr ||
        point_priority < direct_candidate_priority ||
        (point_priority == direct_candidate_priority && point_candidate.projectile_miss_distance + 1.0f < direct_candidate.projectile_miss_distance) ||
        (point_priority == direct_candidate_priority && std::fabs(point_candidate.projectile_miss_distance - direct_candidate.projectile_miss_distance) <= 1.0f &&
         point_candidate.fov < direct_candidate.fov) ||
        (point_priority == direct_candidate_priority && std::fabs(point_candidate.fov - direct_candidate.fov) <= 0.01f &&
         point_candidate.distance < direct_candidate.distance)) {
      direct_candidate_priority = point_priority;
      if (config.aimbot.projectile_debug) {
        proj_aim_store_debug_path(debug_path, intercept, point_candidate);
        ++proj_aim_current_debug_stats.direct_candidates;
        proj_aim_current_debug_stats.best_direct = true;
        proj_aim_current_debug_stats.best_time = intercept.intercept_time;
        proj_aim_current_debug_stats.best_fov = direct_fov;
        proj_aim_current_debug_stats.best_direct_miss = intercept.miss_distance;
      }
      direct_candidate = point_candidate;
    }
  }

  if (config.aimbot.projectile_debug) {
    proj_aim_commit_debug_stats();
  }
  return direct_candidate;
}

inline float proj_aim_direct_prediction_horizon(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const proj_aim_weapon_profile& profile) {
  if (localplayer == nullptr || weapon == nullptr || player == nullptr || profile.params.speed <= 0.0f) {
    return profile.params.max_time;
  }

  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  const float distance = distance_3d(shoot_pos, player->get_origin());
  const float flight_time = distance / std::max(profile.params.speed, 1.0f);
  const float lead_time = local_prediction_ticks_to_time(local_prediction_network_lead_ticks(player));
  const float pad_time = profile.arcing ? 0.45f : 0.25f;
  return std::clamp(
    flight_time + lead_time + pad_time,
    std::max(0.18f, static_cast<float>(TICK_INTERVAL) * 8.0f),
    profile.params.max_time);
}

inline aimbot_candidate proj_aim_find_candidate(Player* localplayer, Weapon* weapon, Player* player, user_cmd* user_cmd, const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr || weapon == nullptr || player == nullptr || user_cmd == nullptr) return candidate;

  if (aimbot_simple_simulation_enabled()) {
    return proj_aim_find_simple_candidate(localplayer, weapon, player, user_cmd, original_view_angles);
  }

  const proj_aim_weapon_profile profile = proj_aim_profile_for_weapon(weapon);
  if (profile.params.speed <= 0.0f) {
    return candidate;
  }

  proj_aim_budget_guard budget_scope{};
  proj_aim_budget_begin_for_distance(distance_3d(localplayer->get_shoot_pos(), player->get_origin()));

  const bool direct_only = config.aimbot.projectile_mode == Aim::ProjectileMode::DIRECT_ONLY;
  const float target_path_horizon = direct_only
    ? proj_aim_direct_prediction_horizon(localplayer, weapon, player, profile)
    : profile.params.max_time;
  LocalPredictionEntityPath target_path = local_prediction_predict_entity_path(player, target_path_horizon, true, true);
  if (!target_path.valid || target_path.positions.empty()) {
    return candidate;
  }

  const uint32_t configured_hitbox_mask = config.aimbot.projectile_hitboxes;
  const uint32_t hitbox_mask = proj_aim_effective_hitbox_mask(localplayer, weapon, player);
  proj_aim_reset_debug_stats(weapon, player, target_path, configured_hitbox_mask, hitbox_mask);
  const std::vector<proj_aim_direct_point> direct_points = proj_aim_direct_points(localplayer, weapon, player, hitbox_mask);
  if (config.aimbot.projectile_debug) {
    proj_aim_current_debug_stats.direct_points = static_cast<int>(direct_points.size());
  }
  std::vector<proj_aim_direct_history> direct_history{};
  if (config.aimbot.projectile_debug) {
    direct_history.reserve(direct_points.size());
  }

  aimbot_candidate direct_candidate{};
  int direct_candidate_priority = INT_MAX;
  if (profile.supports_direct) {
    for (const proj_aim_direct_point& sample : direct_points) {
      if (config.aimbot.projectile_debug) {
        ++proj_aim_current_debug_stats.direct_solves;
      }
      LocalPredictionInterceptResult intercept = local_prediction_find_projectile_intercept(
        localplayer,
        weapon,
        target_path,
        sample.offset,
        user_cmd,
        profile.params.max_time);
      if (!intercept.valid || intercept.intercept_time < profile.arm_time) {
        continue;
      }
      if (config.aimbot.projectile_debug) {
        ++proj_aim_current_debug_stats.direct_intercepts;
      }

      const float direct_fov = aimbot_calculate_fov(intercept.aim_angles, original_view_angles);
      if (!aimbot_fov_within_limit(direct_fov, 1.2f, 3.0f)) {
        continue;
      }

      if (!proj_aim_trace_path(localplayer, player, weapon, intercept)) {
        if (config.aimbot.projectile_debug) {
          ++proj_aim_current_debug_stats.direct_trace_rejects;
        }
        continue;
      }

      if (config.aimbot.projectile_debug) {
        direct_history.push_back({
          .predicted_origin = intercept.has_target_base_origin
            ? intercept.target_base_origin
            : intercept.target_origin - sample.offset,
          .point = sample,
          .intercept = intercept,
          .fov = direct_fov
        });
      }

      aimbot_candidate point_candidate{};
      point_candidate.entity = player;
      point_candidate.player = player;
      point_candidate.preferred = aimbot::has_preference(player);
      point_candidate.bone = sample.bone;
      point_candidate.hitbox = sample.hitbox;
      point_candidate.aim_position = intercept.target_origin;
      point_candidate.aim_angles = intercept.aim_angles;
      point_candidate.fov = direct_fov;
      point_candidate.distance = intercept.intercept_distance;
      point_candidate.health = player->get_health();
      point_candidate.visible = true;
      point_candidate.projectile_direct = true;
      point_candidate.projectile_has_target_base_origin = intercept.has_target_base_origin;
      point_candidate.projectile_intercept_time = intercept.intercept_time;
      point_candidate.projectile_miss_distance = intercept.miss_distance;
      point_candidate.projectile_target_base_origin = intercept.target_base_origin;
      point_candidate.projectile_target_offset = intercept.target_offset;

      const int point_priority = sample.priority;

      if (direct_candidate.entity == nullptr ||
          point_priority < direct_candidate_priority ||
          (point_priority == direct_candidate_priority && point_candidate.projectile_miss_distance + 1.0f < direct_candidate.projectile_miss_distance) ||
          (point_priority == direct_candidate_priority && std::fabs(point_candidate.projectile_miss_distance - direct_candidate.projectile_miss_distance) <= 1.0f &&
           point_candidate.fov < direct_candidate.fov) ||
          (point_priority == direct_candidate_priority && std::fabs(point_candidate.fov - direct_candidate.fov) <= 0.01f &&
           point_candidate.distance < direct_candidate.distance)) {
        proj_aim_store_debug_path(target_path, intercept, point_candidate);
        direct_candidate_priority = point_priority;
        if (config.aimbot.projectile_debug) {
          ++proj_aim_current_debug_stats.direct_candidates;
          proj_aim_current_debug_stats.best_direct = true;
          proj_aim_current_debug_stats.best_time = intercept.intercept_time;
          proj_aim_current_debug_stats.best_fov = direct_fov;
          proj_aim_current_debug_stats.best_direct_miss = intercept.miss_distance;
        }
        direct_candidate = point_candidate;
      }
    }
  }

  const bool direct_confident = proj_aim_direct_candidate_confident(profile, direct_candidate);
  if (config.aimbot.projectile_mode == Aim::ProjectileMode::DIRECT_THEN_SPLASH && direct_candidate.player != nullptr) {
    if (config.aimbot.projectile_debug) {
      proj_aim_last_direct_history = std::move(direct_history);
      proj_aim_last_splash_history.clear();
      proj_aim_commit_debug_stats();
    }
    return direct_candidate;
  }
  if (config.aimbot.projectile_mode == Aim::ProjectileMode::DIRECT_THEN_SPLASH && direct_confident) {
    if (config.aimbot.projectile_debug) {
      proj_aim_last_direct_history = std::move(direct_history);
      proj_aim_last_splash_history.clear();
      proj_aim_commit_debug_stats();
    }
    return direct_candidate;
  }

  aimbot_candidate splash_candidate{};
  if (profile.supports_splash) {
    splash_candidate = proj_aim_find_splash_candidate(
      localplayer,
      weapon,
      player,
      user_cmd,
      original_view_angles,
      target_path);
  }

  if (config.aimbot.projectile_debug) {
    proj_aim_last_direct_history = std::move(direct_history);
  }

  aimbot_candidate result{};
  switch (config.aimbot.projectile_mode) {
  case Aim::ProjectileMode::DIRECT_ONLY:
    result = direct_candidate;
    break;
  case Aim::ProjectileMode::DIRECT_THEN_SPLASH:
    if (direct_candidate.player != nullptr && (splash_candidate.player == nullptr || direct_confident)) {
      result = direct_candidate;
      break;
    }
    result = splash_candidate.player != nullptr ? splash_candidate : direct_candidate;
    break;
  case Aim::ProjectileMode::PREFER_SPLASH:
    result = splash_candidate.player != nullptr ? splash_candidate : direct_candidate;
    break;
  case Aim::ProjectileMode::SPLASH_ONLY:
    result = splash_candidate;
    break;
  default:
    result = direct_candidate;
    break;
  }

  if (config.aimbot.projectile_debug) {
    proj_aim_commit_debug_stats();
  }
  return result;
}

#endif
