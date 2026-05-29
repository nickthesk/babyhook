/*
/^-----^\   data: 2026-05-16
V  o o  V  file: src/features/combat/aimbot/projectile/projectile_live_data.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef PROJECTILE_LIVE_DATA_HPP
#define PROJECTILE_LIVE_DATA_HPP

#include <algorithm>
#include <cmath>

#include "core/shared/sigs.hpp"
#include "libsigscan/libsigscan.h"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

inline float projectile_convar_float(Convar*& convar, const char* name, float fallback_value) {
  if (name == nullptr || convar_system == nullptr) {
    return fallback_value;
  }

  if (convar == nullptr) {
    convar = convar_system->find_var(name);
  }

  if (convar == nullptr) {
    return fallback_value;
  }

  const float value = convar->get_float();
  return std::isfinite(value) ? value : fallback_value;
}

inline float projectile_attr_float(Weapon* weapon, float base_value, const char* attribute_name) {
  if (weapon == nullptr || attribute_manager == nullptr || attribute_name == nullptr) {
    return base_value;
  }

  const float value = attribute_manager->attrib_hook_value(base_value, attribute_name, weapon->to_entity());
  return std::isfinite(value) ? value : base_value;
}

inline float projectile_weapon_data_speed_or(Weapon* weapon, float fallback_speed) {
  if (weapon == nullptr) {
    return fallback_speed;
  }

  const float data_speed = weapon->get_projectile_speed_from_data();
  if (std::isfinite(data_speed) && data_speed > 1.0f) {
    return data_speed;
  }
  return fallback_speed;
}

inline float projectile_speed_attr(Weapon* weapon, float base_speed) {
  return projectile_attr_float(weapon, base_speed, "mult_projectile_speed");
}

inline float projectile_range_speed_attr(Weapon* weapon, float base_speed) {
  return projectile_attr_float(weapon, projectile_speed_attr(weapon, base_speed), "mult_projectile_range");
}

inline float projectile_flamethrower_life_attr(Weapon* weapon, float base_value) {
  return projectile_attr_float(weapon, base_value, "mult_flame_life");
}

inline float projectile_flamethrower_size_attr(Weapon* weapon, float base_value) {
  return projectile_attr_float(weapon, base_value, "mult_flame_size");
}

inline float projectile_flamethrower_speed(Weapon* weapon) {
  static Convar* tf_flamethrower_velocity = nullptr;
  return projectile_flamethrower_life_attr(weapon, projectile_convar_float(tf_flamethrower_velocity, "tf_flamethrower_velocity", 2300.0f));
}

inline float projectile_flamethrower_lifetime(Weapon* weapon) {
  static Convar* tf_flamethrower_flametime = nullptr;
  return projectile_flamethrower_life_attr(weapon, projectile_convar_float(tf_flamethrower_flametime, "tf_flamethrower_flametime", 0.5f));
}

inline float projectile_flamethrower_drag_factor() {
  static Convar* tf_flamethrower_drag = nullptr;
  return std::clamp(projectile_convar_float(tf_flamethrower_drag, "tf_flamethrower_drag", 0.87f), 0.0f, 1.0f);
}

inline float projectile_flamethrower_upward_velocity() {
  static Convar* tf_flamethrower_float = nullptr;
  return projectile_convar_float(tf_flamethrower_float, "tf_flamethrower_float", 50.0f);
}

inline float projectile_flamethrower_hull_radius(Weapon* weapon) {
  static Convar* tf_flamethrower_boxsize = nullptr;
  const float box_size = projectile_convar_float(tf_flamethrower_boxsize, "tf_flamethrower_boxsize", 12.0f);
  return std::clamp(projectile_flamethrower_size_attr(weapon, box_size), 1.0f, 48.0f);
}

inline float projectile_flamethrower_max_damage_distance() {
  static Convar* tf_flamethrower_maxdamagedist = nullptr;
  return std::clamp(projectile_convar_float(tf_flamethrower_maxdamagedist, "tf_flamethrower_maxdamagedist", 350.0f), 64.0f, 1024.0f);
}

inline float projectile_flamethrower_effective_range(Weapon* weapon) {
  const float tick_interval = global_vars != nullptr && global_vars->interval_per_tick > 0.0f
    ? global_vars->interval_per_tick
    : static_cast<float>(TICK_INTERVAL);
  const float lifetime = std::clamp(projectile_flamethrower_lifetime(weapon), tick_interval, 3.0f);
  const int tick_count = std::clamp(static_cast<int>(std::ceil(lifetime / tick_interval)), 1, 256);
  float speed = std::clamp(projectile_flamethrower_speed(weapon), 1.0f, 5000.0f);
  const float drag = projectile_flamethrower_drag_factor();
  float range = 0.0f;
  float time = 0.0f;
  for (int tick = 0; tick < tick_count && time < lifetime; ++tick) {
    const float dt = std::min(tick_interval, lifetime - time);
    range += speed * dt;
    speed *= drag;
    time += dt;
  }

  return std::min(range, projectile_flamethrower_max_damage_distance());
}

inline float projectile_sticky_arm_time_live(Weapon* weapon, float fallback_arm_time = 0.8f) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  using sticky_arm_time_fn = float (*)(Weapon*);
  static sticky_arm_time_fn sticky_arm_time = nullptr;
  static bool sticky_arm_time_initialized = false;
  if (!sticky_arm_time_initialized) {
    sticky_arm_time_initialized = true;
    sticky_arm_time = reinterpret_cast<sticky_arm_time_fn>(sigscan_module("client.so", sigs::tf_projectile_sticky_arm_time));
    if (sticky_arm_time == nullptr) {
      sticky_arm_time = reinterpret_cast<sticky_arm_time_fn>(sigscan_module("server.so", sigs::tf_projectile_sticky_arm_time));
    }
  }

  if (sticky_arm_time != nullptr) {
    const float arm_time = sticky_arm_time(weapon);
    if (std::isfinite(arm_time) && arm_time >= 0.0f && arm_time <= 5.0f) {
      return arm_time;
    }
  }

  return projectile_attr_float(weapon, fallback_arm_time, "sticky_arm_time");
}

#endif
