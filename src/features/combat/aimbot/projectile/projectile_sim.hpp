#ifndef PROJECTILE_SIM_HPP
#define PROJECTILE_SIM_HPP

#include "features/movement/local_prediction/move_sim.hpp"
#include "features/combat/aimbot/projectile/projectile_types.hpp"
#include "features/combat/aimbot/projectile/projectile_live_data.hpp"
#include "features/combat/aimbot/projectile/projectile_trace.hpp"
#include "features/combat/aimbot/proj_aim/proj_aim_budget.hpp"
#include "features/menu/config.hpp"
inline bool local_prediction_flip_projectile_offset_y() {
  static Convar* cl_flipviewmodels = nullptr;
  if (cl_flipviewmodels == nullptr && convar_system != nullptr) {
    cl_flipviewmodels = convar_system->find_var("cl_flipviewmodels");
  }

  return cl_flipviewmodels != nullptr && cl_flipviewmodels->get_int() != 0;
}



inline Vec3 local_prediction_projectile_offset_for_weapon(Player* localplayer, Weapon* weapon) {
  if (localplayer == nullptr || weapon == nullptr) {
    return Vec3{};
  }

  const bool ducking = localplayer->is_ducking();
  switch (weapon->get_def_id()) {
  case Soldier_m_TheCowMangler5000:
  case Soldier_s_TheRighteousBison:
    return Vec3{23.5f, 8.0f, ducking ? 8.0f : -3.0f};
  case Engi_m_ThePomson6000:
    return Vec3{23.5f, 8.0f, (ducking ? 8.0f : -3.0f) - 13.0f};
  case Pyro_m_DragonsFury:
    return Vec3{3.0f, 7.0f, -9.0f};
  case Soldier_m_RocketLauncher:
  case Soldier_m_RocketLauncherR:
  case Soldier_m_TheDirectHit:
  case Soldier_m_TheBlackBox:
  case Soldier_m_RocketJumper:
  case Soldier_m_TheLibertyLauncher:
  case Soldier_m_TheOriginal:
  case Soldier_m_FestiveRocketLauncher:
  case Soldier_m_TheBeggarsBazooka:
  case Soldier_m_FestiveBlackBox:
  case Soldier_m_TheAirStrike:
    return Vec3{23.5f, attribute_manager != nullptr && static_cast<int>(attribute_manager->attrib_hook_value(0.0f, "centerfire_projectile", weapon->to_entity())) == 1 ? 0.0f : 12.0f, ducking ? 8.0f : -3.0f};
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
    return Vec3{16.0f, 8.0f, -6.0f};
  case Pyro_s_TheFlareGun:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheManmelter:
  case Pyro_s_TheScorchShot:
  case Pyro_s_FestiveFlareGun:
    return Vec3{23.5f, 12.0f, ducking ? 8.0f : -3.0f};
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
  case Engi_m_TheRescueRanger:
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
    return Vec3{23.5f, 8.0f, -3.0f};
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
    return Vec3{16.0f, 6.0f, -8.0f};
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
    return Vec3{16.0f, 8.0f, -6.0f};
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return Vec3{16.0f, 8.0f, -6.0f};
  default:
    return Vec3{};
  }
}



inline LocalPredictionLaunchState local_prediction_build_launch_state(Player* localplayer, user_cmd* user_cmd) {
  LocalPredictionLaunchState state{};
  if (localplayer == nullptr || user_cmd == nullptr) return state;
  state.origin = localplayer->get_shoot_pos();
  state.view_angles = user_cmd->view_angles;
  state.direction = local_prediction_normalize(local_prediction_angles_to_direction(state.view_angles));
  state.inherited_velocity = localplayer->get_velocity();
  return state;
}

inline LocalPredictionLaunchState local_prediction_build_launch_state(Player* localplayer, Weapon* weapon, user_cmd* user_cmd) {
  LocalPredictionLaunchState state = local_prediction_build_launch_state(localplayer, user_cmd);
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr) {
    return state;
  }

  const Vec3 offset = local_prediction_projectile_offset_for_weapon(localplayer, weapon);
  if (offset.x == 0.0f && offset.y == 0.0f && offset.z == 0.0f) {
    return state;
  }

  Vec3 forward{};
  Vec3 right{};
  Vec3 up{};
  angle_vectors(state.view_angles, &forward, &right, &up);
  state.origin += (forward * offset.x) + (right * offset.y) + (up * offset.z);
  return state;
}

inline LocalPredictionProjectileTrace local_prediction_simulate_projectile(const LocalPredictionLaunchState& launch_state,
  const LocalPredictionProjectileParameters& params) {
  LocalPredictionProjectileTrace trace{};
  if (params.speed <= 0.0f || params.time_step <= 0.0f || params.max_time <= 0.0f) return trace;

  Vec3 position = launch_state.origin;
  Vec3 velocity{
    launch_state.direction.x * params.speed + launch_state.inherited_velocity.x,
    launch_state.direction.y * params.speed + launch_state.inherited_velocity.y,
    launch_state.direction.z * params.speed + launch_state.inherited_velocity.z
  };

  int step_count = std::max(1, static_cast<int>(std::ceil(params.max_time / params.time_step)));
  trace.steps.reserve(step_count + 1);
  for (int step_index = 0; step_index <= step_count; ++step_index) {
    float time = std::min(params.max_time, step_index * params.time_step);
    LocalPredictionProjectileStep step{};
    step.time = time;
    step.position = position;
    step.velocity = velocity;
    trace.steps.push_back(step);

    if (time >= params.max_time) break;
    position.x += velocity.x * params.time_step;
    position.y += velocity.y * params.time_step;
    position.z += velocity.z * params.time_step;
    velocity.z -= params.gravity * params.time_step;
  }

  trace.valid = !trace.steps.empty();
  return trace;
}

inline LocalPredictionProjectileParameters local_prediction_projectile_parameters_for_weapon(Weapon* weapon) {
  LocalPredictionProjectileParameters params{};
  if (weapon == nullptr) return params;

  const float gravity_scale = local_prediction_projectile_gravity_scale();
  const auto projectile_speed = [weapon](float base_speed) {
    return projectile_speed_attr(weapon, projectile_weapon_data_speed_or(weapon, base_speed));
  };
  const auto projectile_range_speed = [weapon](float base_speed) {
    return projectile_range_speed_attr(weapon, projectile_weapon_data_speed_or(weapon, base_speed));
  };
  const auto attribute_value = [weapon](float base_value, const char* attribute_name) {
    return projectile_attr_float(weapon, base_value, attribute_name);
  };

  switch (weapon->get_def_id()) {
  case Soldier_m_RocketLauncher:
  case Soldier_m_RocketLauncherR:
  case Soldier_m_TheBlackBox:
  case Soldier_m_RocketJumper:
  case Soldier_m_TheLibertyLauncher:
  case Soldier_m_TheCowMangler5000:
  case Soldier_m_TheOriginal:
  case Soldier_m_FestiveRocketLauncher:
  case Soldier_m_TheBeggarsBazooka:
  case Soldier_m_FestiveBlackBox:
  case Soldier_m_TheAirStrike:
    params.speed = projectile_speed(1100.0f);
    params.gravity = 0.0f;
    params.max_time = 6.0f;
    break;
  case Soldier_m_TheDirectHit:
    params.speed = projectile_speed(1100.0f);
    params.gravity = 0.0f;
    params.max_time = 4.0f;
    break;
  case Soldier_s_TheRighteousBison:
    params.speed = projectile_speed(1200.0f);
    params.gravity = 0.0f;
    params.max_time = 5.0f;
    break;
  case Engi_m_ThePomson6000:
    params.speed = projectile_speed(1200.0f);
    params.gravity = 0.0f;
    params.max_time = 5.0f;
    break;
  case Pyro_m_DragonsFury:
    {
      static Convar* tf_fireball_speed = nullptr;
      static Convar* tf_fireball_distance = nullptr;
      static Convar* tf_fireball_max_lifetime = nullptr;
      if (convar_system != nullptr) {
        if (tf_fireball_speed == nullptr) {
          tf_fireball_speed = convar_system->find_var("tf_fireball_speed");
        }
        if (tf_fireball_distance == nullptr) {
          tf_fireball_distance = convar_system->find_var("tf_fireball_distance");
        }
        if (tf_fireball_max_lifetime == nullptr) {
          tf_fireball_max_lifetime = convar_system->find_var("tf_fireball_max_lifetime");
        }
      }

      const float speed = tf_fireball_speed != nullptr ? tf_fireball_speed->get_float() : 3000.0f;
      const float distance = tf_fireball_distance != nullptr ? tf_fireball_distance->get_float() : 1200.0f;
      const float max_lifetime = tf_fireball_max_lifetime != nullptr ? tf_fireball_max_lifetime->get_float() : 0.8f;
      params.speed = projectile_speed(speed);
      params.max_time = std::min(distance / std::max(params.speed, 1.0f), max_lifetime);
    }
    params.gravity = 0.0f;
    break;
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
  case Engi_m_TheRescueRanger:
    params.speed = projectile_speed(2400.0f);
    params.gravity = 0.2f * gravity_scale * 800.0f;
    params.max_time = 4.0f;
    break;
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
    {
      const float charge_begin_time = weapon->get_charge_begin_time();
      const float held_time = charge_begin_time > 0.0f && global_vars != nullptr
        ? std::clamp(global_vars->curtime - charge_begin_time, 0.0f, 1.0f)
        : 0.0f;
      const float charge = std::clamp(held_time, 0.0f, 1.0f);
      params.speed = 1800.0f + ((2600.0f - 1800.0f) * charge);
      params.gravity = (0.5f + ((0.1f - 0.5f) * charge)) * gravity_scale * 800.0f;
    }
    params.max_time = 4.0f;
    break;
  case Pyro_s_TheFlareGun:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheScorchShot:
  case Pyro_s_FestiveFlareGun:
    params.speed = projectile_speed(2000.0f);
    params.gravity = 0.3f * gravity_scale * 800.0f;
    params.max_time = 1.8f;
    break;
  case Pyro_s_TheManmelter:
    params.speed = projectile_speed(3000.0f);
    params.gravity = 0.45f * gravity_scale * 800.0f;
    params.max_time = 1.8f;
    break;
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_TheLooseCannon:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber: {
    params.speed = projectile_range_speed(1200.0f);
    params.gravity = gravity_scale * 800.0f;
    constexpr float grenade_check_interval = 0.195f;
    float lifetime = 0.0f;
    if (weapon->get_def_id() == Demoman_m_TheLooseCannon) {
      const float mortar_time = attribute_value(0.0f, "grenade_launcher_mortar_mode");
      if (mortar_time > 0.0f) {
        const float detonate_time = weapon->get_detonate_time();
        lifetime = detonate_time > 0.0f && global_vars != nullptr
          ? detonate_time - global_vars->curtime
          : mortar_time;
        lifetime = std::clamp(lifetime, static_cast<float>(TICK_INTERVAL), mortar_time);
      } else {
        lifetime = attribute_value(2.0f, "fuse_mult");
      }
    } else {
      lifetime = attribute_value(2.0f, "fuse_mult");
    }
    lifetime = std::ceil(lifetime / grenade_check_interval) * grenade_check_interval;
    params.max_time = std::max(lifetime, static_cast<float>(TICK_INTERVAL));
    break;
  }
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
    params.speed = projectile_speed(1000.0f);
    params.gravity = gravity_scale * 800.0f;
    params.max_time = 2.2f;
    break;
  case Pyro_s_GasPasser:
    params.speed = projectile_speed(2000.0f);
    params.gravity = gravity_scale * 800.0f;
    params.max_time = 2.2f;
    break;
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    params.speed = projectile_speed(3000.0f);
    params.gravity = gravity_scale * 800.0f;
    params.max_time = 2.2f;
    break;
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
    params.speed = projectile_speed(1000.0f);
    params.gravity = 0.3f * gravity_scale * 800.0f;
    params.max_time = 2.0f;
    break;
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    {
      const float charge_rate = attribute_manager != nullptr
        ? attribute_manager->attrib_hook_value(4.0f, "stickybomb_charge_rate", weapon->to_entity())
        : 4.0f;
      const float charge_begin_time = weapon->get_charge_begin_time();
      const float held_time = charge_begin_time > 0.0f && global_vars != nullptr
        ? std::clamp(global_vars->curtime - charge_begin_time, 0.0f, charge_rate)
        : 0.0f;
      const float charge = charge_rate > 0.0f ? std::clamp(held_time / charge_rate, 0.0f, 1.0f) : 0.0f;
      params.speed = projectile_range_speed(900.0f + ((2400.0f - 900.0f) * charge));
      params.gravity = gravity_scale * 800.0f;
      const int def = weapon->get_def_id();
      if (def == Demoman_s_TheScottishResistance) {
        params.max_time = 2.45f;
      } else if (def == Demoman_s_TheQuickiebombLauncher) {
        params.max_time = 1.7f;
      } else {
        params.max_time = 2.0f;
      }
    }
    break;
  default:
    break;
  }

  return params;
}

inline bool projectile_sim_is_grenade_like_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
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
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
    return true;
  default:
    return false;
  }
}

inline bool projectile_sim_is_rocket_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

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
    return true;
  default:
    return false;
  }
}

inline bool projectile_sim_uses_pipe_fire_setup(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
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
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return true;
  default:
    return false;
  }
}

inline projectile_sim_velocity_mode projectile_sim_velocity_mode_for_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return projectile_sim_velocity_mode::forward;
  }

  switch (weapon->get_def_id()) {
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return projectile_sim_velocity_mode::cleaver;
  default:
    return projectile_sim_is_grenade_like_weapon(weapon)
      ? projectile_sim_velocity_mode::pipe_lift
      : projectile_sim_velocity_mode::forward;
  }
}

inline float projectile_sim_drag_for_weapon(Weapon* weapon, float speed) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  const auto remap_clamped = [](float value, float in_min, float in_max, float out_min, float out_max) {
    const float clamped = std::clamp(value, in_min, in_max);
    return out_min + ((clamped - in_min) / std::max(in_max - in_min, 0.0001f)) * (out_max - out_min);
  };

  switch (weapon->get_def_id()) {
  case Demoman_m_TheLochnLoad:
    return remap_clamped(speed, 1504.0f, 3500.0f, 0.070f, 0.085f);
  case Demoman_m_TheLooseCannon:
    return remap_clamped(speed, 1454.0f, 3500.0f, 0.385f, 0.530f);
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
    return remap_clamped(speed, 1217.0f, 3500.0f, 0.100f, 0.200f);
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    return remap_clamped(speed, 922.0f, 2400.0f, 0.085f, 0.190f);
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
    return 0.057f;
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return 0.310f;
  default:
    return 0.0f;
  }
}

inline Vec3 projectile_sim_drag_basis_for_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return Vec3{};
  }

  switch (weapon->get_def_id()) {
  case Demoman_m_TheLooseCannon:
    return Vec3{0.020971f, 0.019420f, 0.020971f};
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
    return Vec3{0.003902f, 0.009962f, 0.009962f};
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    return Vec3{0.007491f, 0.007491f, 0.007306f};
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
    return Vec3{0.005127f, 0.002925f, 0.004337f};
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return Vec3{0.022287f, 0.005208f, 0.110697f};
  default:
    return Vec3{};
  }
}

inline Vec3 projectile_sim_angular_drag_basis_for_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return Vec3{};
  }

  switch (weapon->get_def_id()) {
  case Demoman_m_TheLooseCannon:
    return Vec3{0.012997f, 0.013496f, 0.013714f};
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
    return Vec3{0.003618f, 0.001514f, 0.001514f};
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    return Vec3{0.002777f, 0.002842f, 0.002812f};
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
    return Vec3{0.003467f, 0.002925f, 0.001995f};
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return Vec3{0.013982f, 0.043243f, 0.003465f};
  default:
    return Vec3{};
  }
}

inline Vec3 projectile_sim_angular_velocity_for_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return Vec3{};
  }

  switch (weapon->get_def_id()) {
  case Demoman_m_TheLooseCannon:
    return Vec3{600.0f, 1200.0f, 0.0f};
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
    return Vec3{600.0f, 0.0f, 0.0f};
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    return Vec3{1200.0f, 0.0f, 0.0f};
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
    return Vec3{300.0f, 0.0f, 0.0f};
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return Vec3{0.0f, 1500.0f, 0.0f};
  default:
    return Vec3{};
  }
}

inline Vec3 projectile_sim_hull_for_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return Vec3{2.0f, 2.0f, 2.0f};
  }

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
    return Vec3{0.0f, 0.0f, 0.0f};
  case Soldier_s_TheRighteousBison:
  case Engi_m_ThePomson6000:
  case Pyro_m_DragonsFury:
    return Vec3{1.0f, 1.0f, 1.0f};
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber: {
    const bool no_spin = attribute_manager != nullptr &&
      attribute_manager->attrib_hook_value(0.0f, "grenade_no_spin", weapon->to_entity()) != 0.0f;
    const float side = no_spin ? 4.0f : 5.0f;
    return Vec3{side, side, side};
  }
  case Demoman_m_TheLooseCannon:
    return Vec3{6.0f, 6.0f, 6.0f};
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    return Vec3{5.0f, 5.0f, 5.0f};
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
    return Vec3{3.0f, 3.0f, 3.0f};
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return Vec3{1.0f, 1.0f, 10.0f};
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
    return Vec3{3.0f, 3.0f, 3.0f};
  case Engi_m_TheRescueRanger:
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
    return Vec3{1.0f, 1.0f, 1.0f};
  case Pyro_s_TheFlareGun:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheScorchShot:
  case Pyro_s_FestiveFlareGun:
  case Pyro_s_TheManmelter:
    return Vec3{0.0f, 0.0f, 0.0f};
  default:
    return Vec3{2.0f, 2.0f, 2.0f};
  }
}

inline projectile_sim_profile projectile_sim_profile_for_weapon(Player* localplayer, Weapon* weapon) {
  projectile_sim_profile profile{};
  if (localplayer == nullptr || weapon == nullptr) {
    return profile;
  }

  profile.params = local_prediction_projectile_parameters_for_weapon(weapon);
  if (profile.params.speed <= 0.0f) {
    return profile;
  }

  if (localplayer->in_cond(TF_COND_RUNE_PRECISION) &&
      (projectile_sim_is_rocket_weapon(weapon) || projectile_sim_uses_pipe_fire_setup(weapon))) {
    profile.params.speed = std::max(profile.params.speed, 3000.0f);
  }

  if (projectile_sim_is_rocket_weapon(weapon) && attribute_manager != nullptr) {
    const int rocket_specialist = static_cast<int>(attribute_manager->attrib_hook_value(
      0.0f,
      "rocket_specialist",
      static_cast<Entity*>(localplayer)));
    if (rocket_specialist > 0) {
      const float specialist_scale = 1.15f + ((std::clamp(static_cast<float>(rocket_specialist), 1.0f, 4.0f) - 1.0f) / 3.0f) * (1.6f - 1.15f);
      profile.params.speed = std::min(profile.params.speed * specialist_scale, 3000.0f);
    }
  }

  const float configured_horizon = static_cast<float>(std::clamp(config.aimbot.projectile_prediction_ticks, 8, 420)) *
    static_cast<float>(TICK_INTERVAL);
  profile.params.max_time = std::min(profile.params.max_time, configured_horizon);
  profile.params.time_step = std::max(profile.params.time_step, static_cast<float>(TICK_INTERVAL));
  profile.offset = local_prediction_projectile_offset_for_weapon(localplayer, weapon);
  if (local_prediction_flip_projectile_offset_y()) {
    profile.offset.y *= -1.0f;
  }
  profile.hull = projectile_sim_hull_for_weapon(weapon);
  profile.hull_trace = profile.hull.x > 0.0f || profile.hull.y > 0.0f || profile.hull.z > 0.0f;
  profile.lifetime = profile.params.max_time;
  profile.initial_lift = projectile_sim_is_grenade_like_weapon(weapon) ? 200.0f : 0.0f;
  profile.drag = projectile_sim_drag_for_weapon(weapon, profile.params.speed);
  profile.drag_basis = projectile_sim_drag_basis_for_weapon(weapon);
  profile.angular_drag_basis = projectile_sim_angular_drag_basis_for_weapon(weapon);
  profile.angular_velocity = projectile_sim_angular_velocity_for_weapon(weapon);
  profile.trace_mask = MASK_SOLID;
  profile.inherit_velocity = false;
  profile.velocity_mode = projectile_sim_velocity_mode_for_weapon(weapon);
  profile.fire_setup_mode = projectile_sim_uses_pipe_fire_setup(weapon)
    ? projectile_sim_fire_setup_mode::pipe_style
    : projectile_sim_fire_setup_mode::traced_forward;
  profile.spawn_trace_mode = profile.fire_setup_mode == projectile_sim_fire_setup_mode::pipe_style
    ? projectile_sim_spawn_trace_mode::hull
    : projectile_sim_spawn_trace_mode::line;
  profile.spawn_trace_mins = profile.fire_setup_mode == projectile_sim_fire_setup_mode::pipe_style
    ? Vec3{-8.0f, -8.0f, -8.0f}
    : Vec3{};
  profile.spawn_trace_maxs = profile.fire_setup_mode == projectile_sim_fire_setup_mode::pipe_style
    ? Vec3{8.0f, 8.0f, 8.0f}
    : Vec3{};
  profile.physics_sim = physics != nullptr &&
    physics_collision != nullptr &&
    (profile.drag > 0.0f ||
      profile.drag_basis.x != 0.0f ||
      profile.drag_basis.y != 0.0f ||
      profile.drag_basis.z != 0.0f);
  profile.valid = true;
  return profile;
}

inline projectile_sim_launch projectile_sim_build_launch_from_angles(Player* localplayer,
  Weapon* weapon,
  const Vec3& angles,
  const projectile_sim_profile& profile) {
  projectile_sim_launch launch{};
  if (localplayer == nullptr || weapon == nullptr || !profile.valid || engine_trace == nullptr) {
    return launch;
  }

  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  launch.angles = angles;
  launch.inherited_velocity = profile.inherit_velocity ? localplayer->get_velocity() : Vec3{};

  Vec3 forward{};
  Vec3 right{};
  Vec3 up{};
  angle_vectors(angles, &forward, &right, &up);
  Vec3 muzzle_pos = shoot_pos;
  if (profile.offset.x != 0.0f || profile.offset.y != 0.0f || profile.offset.z != 0.0f) {
    muzzle_pos += (forward * profile.offset.x) + (right * profile.offset.y) + (up * profile.offset.z);
  }

  if (profile.spawn_trace_mode != projectile_sim_spawn_trace_mode::none) {
    trace_t spawn_trace{};
    const bool use_hull = profile.spawn_trace_mode == projectile_sim_spawn_trace_mode::hull;
    const bool spawn_traced = projectile_trace_ray(
      shoot_pos,
      muzzle_pos,
      use_hull ? &profile.spawn_trace_mins : nullptr,
      use_hull ? &profile.spawn_trace_maxs : nullptr,
      projectile_trace_contract::spawn,
      localplayer->to_entity(),
      -1,
      &spawn_trace);
    if (!spawn_traced) {
      return {};
    }
    if (spawn_trace.start_solid || spawn_trace.all_solid) {
      return {};
    }
    muzzle_pos = spawn_trace.endpos;
  }

  Vec3 launch_direction{};
  if (profile.fire_setup_mode == projectile_sim_fire_setup_mode::traced_forward) {
    Vec3 end_pos = shoot_pos + (forward * 2000.0f);
    trace_t trace{};
    const bool traced = projectile_trace_ray(
      shoot_pos,
      end_pos,
      nullptr,
      nullptr,
      projectile_trace_contract::fire_setup,
      localplayer->to_entity(),
      static_cast<int>(localplayer->get_team()),
      &trace);
    if (traced && trace.fraction > 0.1f && trace.fraction < 1.0f && !trace.start_solid) {
      end_pos = trace.endpos;
    }

    launch_direction = local_prediction_normalize(end_pos - muzzle_pos);
  } else {
    launch_direction = local_prediction_normalize(local_prediction_angles_to_direction(angles));
  }

  launch.direction = launch_direction;
  launch.origin = muzzle_pos;
  launch.valid = !local_prediction_vec3_is_zero(launch.direction);
  return launch;
}

inline projectile_sim_launch projectile_sim_build_launch(Player* localplayer,
  Weapon* weapon,
  user_cmd* user_cmd,
  const projectile_sim_profile& profile) {
  if (user_cmd == nullptr) {
    return {};
  }

  return projectile_sim_build_launch_from_angles(localplayer, weapon, user_cmd->view_angles, profile);
}

inline Vec3 projectile_sim_initial_velocity(const projectile_sim_launch& launch, const projectile_sim_profile& profile) {
  Vec3 velocity{};
  if (profile.velocity_mode == projectile_sim_velocity_mode::pipe_lift) {
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
    angle_vectors(launch.angles, &forward, &right, &up);
    velocity = (forward * profile.params.speed) + (up * profile.initial_lift);
  } else if (profile.velocity_mode == projectile_sim_velocity_mode::cleaver) {
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
    angle_vectors(launch.angles, &forward, &right, &up);
    velocity = local_prediction_normalize((forward * 10.0f) + up) * profile.params.speed;
  } else {
    velocity = Vec3{
      launch.direction.x * profile.params.speed,
      launch.direction.y * profile.params.speed,
      launch.direction.z * profile.params.speed
    };
  }
  velocity += launch.inherited_velocity;
  return velocity;
}

inline Vec3 projectile_sim_apply_drag(const Vec3& velocity, const projectile_sim_profile& profile, float dt) {
  if (profile.drag <= 0.0f || dt <= 0.0f) {
    return velocity;
  }

  const float drag_scale = std::clamp(1.0f - (profile.drag * dt), 0.0f, 1.0f);
  return velocity * drag_scale;
}

inline bool projectile_sim_trace_step(const Vec3& start,
  const Vec3& end,
  const projectile_sim_profile& profile,
  Entity* skip_entity,
  Entity* target_entity,
  projectile_sim_trace_mode trace_mode,
  trace_t* trace_out) {
  if (engine_trace == nullptr || trace_mode == projectile_sim_trace_mode::none || trace_out == nullptr) {
    return false;
  }

  Vec3 mins = profile.hull * -1.0f;
  Vec3 maxs = profile.hull;
  if (trace_mode == projectile_sim_trace_mode::world) {
    if (!proj_aim_budget_try_trace_call()) {
      return false;
    }
    ray_t ray = profile.hull_trace
      ? engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end), &mins, &maxs)
      : engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end));
    trace_filter filter{};
    engine_trace->init_world_trace_filter(&filter);
    engine_trace->trace_ray(&ray, MASK_SOLID_BRUSHONLY, &filter, trace_out);
  } else if (trace_mode == projectile_sim_trace_mode::blocking_non_player) {
    if (!projectile_trace_ray(
        start,
        end,
        profile.hull_trace ? &mins : nullptr,
        profile.hull_trace ? &maxs : nullptr,
        projectile_trace_contract::world_block,
        skip_entity,
        -1,
        trace_out)) {
      return false;
    }
  } else {
    if (!proj_aim_budget_try_trace_call()) {
      return false;
    }
    ray_t ray = profile.hull_trace
      ? engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end), &mins, &maxs)
      : engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end));
    trace_filter filter{};
    engine_trace->init_trace_filter(&filter, skip_entity);
    engine_trace->trace_ray(&ray, profile.trace_mask, &filter, trace_out);
  }
  if (trace_out->fraction >= 0.97f && !trace_out->start_solid && !trace_out->all_solid) {
    return false;
  }

  if (trace_mode == projectile_sim_trace_mode::world || trace_mode == projectile_sim_trace_mode::blocking_non_player) {
    return true;
  }

  return true;
}

inline projectile_simulation::~projectile_simulation() {
  shutdown_physics();
}

inline void projectile_simulation::shutdown_physics() {
  if (physics_environment != nullptr && physics_object != nullptr) {
    physics_environment->DestroyObject(physics_object);
  }
  physics_object = nullptr;

  if (physics_collision != nullptr && physics_collide != nullptr) {
    physics_collision->DestroyCollide(physics_collide);
  }
  physics_collide = nullptr;

  if (physics != nullptr && physics_environment != nullptr) {
    physics->DestroyEnvironment(physics_environment);
  }
  physics_environment = nullptr;
  physics_active = false;
}

inline bool projectile_simulation::init_physics() {
  if (!profile.physics_sim || physics == nullptr || physics_collision == nullptr) {
    return false;
  }

  Vec3 hull = profile.hull;
  if (hull.x <= 0.0f && hull.y <= 0.0f && hull.z <= 0.0f) {
    hull = Vec3{1.0f, 1.0f, 1.0f};
  }

  physics_environment = physics->CreateEnvironment();
  if (physics_environment == nullptr) {
    shutdown_physics();
    return false;
  }

  physics_environment->SetGravity(Vec3{0.0f, 0.0f, -profile.params.gravity});
  physics_environment->SetAirDensity(AIR_DENSITY);
  physics_environment->SetSimulationTimestep(profile.params.time_step);
  physics_environment->ResetSimulationClock();

  physics_performanceparams_t performance{};
  performance.Defaults();
  performance.maxVelocity = std::max(k_flMaxVelocity, profile.params.speed * 1.25f);
  performance.maxAngularVelocity = k_flMaxAngularVelocity;
  physics_environment->SetPerformanceSettings(&performance);

  const Vec3 mins = hull * -1.0f;
  const Vec3 maxs = hull;
  physics_collide = physics_collision->BBoxToCollide(mins, maxs);
  if (physics_collide == nullptr) {
    shutdown_physics();
    return false;
  }

  objectparams_t params{};
  params.mass = 5.0f;
  params.inertia = 1.0f;
  params.damping = 0.0f;
  params.rotdamping = 0.0f;
  params.rotInertiaLimit = 0.05f;
  params.pName = "cathook_projectile_sim";
  params.volume = 1.0f;
  params.dragCoefficient = profile.drag;
  params.enableCollisions = false;

  physics_object = physics_environment->CreatePolyObject(
    physics_collide,
    0,
    position,
    launch.angles,
    &params);
  if (physics_object == nullptr) {
    shutdown_physics();
    return false;
  }

  physics_object->EnableGravity(profile.params.gravity > 0.0f);
  physics_object->EnableDrag(profile.drag > 0.0f);
  physics_object->EnableCollisions(false);
  physics_object->EnableMotion(true);
  physics_object->m_dragBasis = profile.drag_basis;
  physics_object->m_angDragBasis = profile.angular_drag_basis;

  float linear_drag = profile.drag;
  float angular_drag = profile.drag;
  physics_object->SetDragCoefficient(&linear_drag, &angular_drag);
  physics_object->SetVelocity(&velocity, &profile.angular_velocity);
  physics_object->Wake();
  physics_active = true;
  return true;
}

inline bool projectile_simulation::step_physics(float dt) {
  if (!physics_active || physics_environment == nullptr || physics_object == nullptr || dt <= 0.0f) {
    return false;
  }

  const Vec3 start = position;
  physics_environment->SetSimulationTimestep(dt);
  physics_environment->Simulate(dt);

  Vec3 next_position{};
  Vec3 next_angles{};
  Vec3 next_velocity{};
  Vec3 next_angular_velocity{};
  physics_object->GetPosition(&next_position, &next_angles);
  physics_object->GetVelocity(&next_velocity, &next_angular_velocity);

  trace_t trace{};
  const bool hit = profile.collide_world &&
    projectile_sim_trace_step(start, next_position, profile, skip_entity, target_entity, trace_mode, &trace);

  if (hit) {
    const float trace_fraction = std::clamp(trace.fraction, 0.0f, 1.0f);
    const float hit_time = time + (dt * trace_fraction);
    const bool hit_target = target_entity != nullptr && trace.entity == target_entity;

    result.steps.emplace_back(projectile_sim_step{
      .time = hit_time,
      .position = trace.endpos,
      .velocity = next_velocity,
      .trace = trace,
      .hit = true,
      .hit_target = hit_target
    });

    result.hit = true;
    result.hit_target = hit_target;
    result.hit_time = hit_time;
    result.hit_position = trace.endpos;
    result.hit_normal = trace.plane.normal;
    result.hit_entity = trace.entity;
    position = trace.endpos;
    velocity = next_velocity;
    time = hit_time;
    finished = true;
    return true;
  }

  position = next_position;
  velocity = next_velocity;
  time += dt;
  ++tick;

  result.steps.emplace_back(projectile_sim_step{
    .time = time,
    .position = position,
    .velocity = velocity
  });
  return true;
}

inline bool projectile_simulation::init(const projectile_sim_launch& launch_in,
  const projectile_sim_profile& profile_in,
  Entity* skip_entity_in,
  Entity* target_entity_in,
  projectile_sim_trace_mode trace_mode_in) {
  shutdown_physics();
  *this = {};
  if (!profile_in.valid || !launch_in.valid || profile_in.params.speed <= 0.0f || profile_in.params.time_step <= 0.0f || profile_in.lifetime <= 0.0f) {
    return false;
  }

  profile = profile_in;
  launch = launch_in;
  skip_entity = skip_entity_in;
  target_entity = target_entity_in;
  trace_mode = trace_mode_in;
  position = launch.origin;
  velocity = projectile_sim_initial_velocity(launch, profile);
  max_ticks = std::clamp(
    static_cast<int>(std::ceil(profile.lifetime / profile.params.time_step)),
    1,
    std::clamp(config.aimbot.projectile_prediction_ticks, 8, 420));

  result.steps.reserve(static_cast<size_t>(max_ticks) + 1);
  result.steps.emplace_back(projectile_sim_step{
    .time = 0.0f,
    .position = position,
    .velocity = velocity
  });
  initialized = true;
  result.valid = true;
  if (trace_mode != projectile_sim_trace_mode::none && profile.collide_world) {
    init_physics();
  }
  return true;
}

inline bool projectile_simulation::step() {
  if (!initialized || finished) {
    return false;
  }

  if (tick >= max_ticks || time >= profile.lifetime) {
    finished = true;
    return false;
  }

  const float dt = std::min(profile.params.time_step, profile.lifetime - time);
  if (physics_active && step_physics(dt)) {
    return !finished;
  }

  const Vec3 next_position{
    position.x + (velocity.x * dt),
    position.y + (velocity.y * dt),
    position.z + (velocity.z * dt) - (0.5f * profile.params.gravity * dt * dt)
  };

  trace_t trace{};
  const bool hit = profile.collide_world &&
    projectile_sim_trace_step(position, next_position, profile, skip_entity, target_entity, trace_mode, &trace);

  if (hit) {
    const float trace_fraction = std::clamp(trace.fraction, 0.0f, 1.0f);
    const float hit_time = time + (dt * trace_fraction);
    const bool hit_target = target_entity != nullptr && trace.entity == target_entity;

    result.steps.emplace_back(projectile_sim_step{
      .time = hit_time,
      .position = trace.endpos,
      .velocity = velocity,
      .trace = trace,
      .hit = true,
      .hit_target = hit_target
    });

    result.hit = true;
    result.hit_target = hit_target;
    result.hit_time = hit_time;
    result.hit_position = trace.endpos;
    result.hit_normal = trace.plane.normal;
    result.hit_entity = trace.entity;
    position = trace.endpos;
    time = hit_time;
    finished = true;
    return false;
  }

  position = next_position;
  velocity.z -= profile.params.gravity * dt;
  velocity = projectile_sim_apply_drag(velocity, profile, dt);
  time += dt;
  ++tick;

  result.steps.emplace_back(projectile_sim_step{
    .time = time,
    .position = position,
    .velocity = velocity
  });
  return true;
}

inline projectile_sim_result projectile_simulation::run() {
  while (step()) {
  }

  return result;
}

inline projectile_sim_result projectile_sim_run(const projectile_sim_launch& launch,
  const projectile_sim_profile& profile,
  Entity* skip_entity = nullptr,
  Entity* target_entity = nullptr,
  projectile_sim_trace_mode trace_mode = projectile_sim_trace_mode::none) {
  const bool traced_sim = trace_mode != projectile_sim_trace_mode::none && profile.collide_world;
  if (traced_sim && !proj_aim_budget_try_sim_call()) {
    return {};
  }

  projectile_simulation sim{};
  if (!sim.init(launch, profile, skip_entity, target_entity, trace_mode)) {
    return {};
  }

  return sim.run();
}

inline Vec3 projectile_sim_direction_for_time(const projectile_sim_launch& launch,
  const projectile_sim_profile& profile,
  const Vec3& target_position,
  float travel_time) {
  if (!profile.valid || profile.params.speed <= 0.0f || travel_time <= 0.0001f) {
    return Vec3{};
  }

  Vec3 needed_velocity = (target_position - launch.origin) * (1.0f / travel_time);
  needed_velocity -= launch.inherited_velocity;
  needed_velocity.z -= profile.initial_lift;
  needed_velocity.z += 0.5f * profile.params.gravity * travel_time;

  return local_prediction_normalize(needed_velocity);
}

inline Vec3 projectile_sim_position_at_time(const projectile_sim_launch& launch,
  const projectile_sim_profile& profile,
  float travel_time) {
  Vec3 velocity = projectile_sim_initial_velocity(launch, profile);
  return Vec3{
    launch.origin.x + (velocity.x * travel_time),
    launch.origin.y + (velocity.y * travel_time),
    launch.origin.z + (velocity.z * travel_time) - (0.5f * profile.params.gravity * travel_time * travel_time)
  };
}

inline bool projectile_sim_calc_angle_to_point(const Vec3& from,
  const Vec3& to,
  const projectile_sim_profile& profile,
  bool high_arc,
  Vec3* angle_out,
  float* time_out) {
  if (angle_out == nullptr || time_out == nullptr || !profile.valid || profile.params.speed <= 1.0f) {
    return false;
  }

  const Vec3 delta = to - from;
  const float dx = std::sqrt((delta.x * delta.x) + (delta.y * delta.y));
  const float dy = delta.z;
  if (dx <= 0.001f) {
    return false;
  }

  Vec3 angle{0.0f, std::atan2(delta.y, delta.x) * radpi, 0.0f};
  float flight_time = 0.0f;
  float speed = profile.params.speed;
  float vertical_lift = profile.initial_lift;
  if (profile.velocity_mode == projectile_sim_velocity_mode::cleaver) {
    constexpr float cleaver_scale = 0.9950371902f;
    constexpr float cleaver_lift_scale = 0.0995037190f;
    speed = profile.params.speed * cleaver_scale;
    vertical_lift = profile.params.speed * cleaver_lift_scale;
  }

  if (profile.params.gravity > 0.0f) {
    const float gravity = profile.params.gravity;
    float work_speed = speed;
    float work_lift = vertical_lift;
    float pitch = 0.0f;
    float horizontal_velocity = 1.0f;
    flight_time = 0.1f;
    constexpr int max_drag_iterations = 6;
    for (int drag_iteration = 0; drag_iteration < max_drag_iterations; ++drag_iteration) {
      const float speed2 = work_speed * work_speed;
      const float lift2 = work_lift * work_lift;
      const float dx2 = dx * dx;

      const float a = (dy * lift2) + (dx * work_speed * work_lift) + (0.5f * gravity * dx2);
      const float b = (-2.0f * dy * work_speed * work_lift) - (dx * (speed2 - lift2));
      const float c = (dy * speed2) - (dx * work_speed * work_lift) + (0.5f * gravity * dx2);
      const float root = (b * b) - (4.0f * a * c);
      if (root < 0.0f || std::fabs(a) <= 0.0001f) {
        return false;
      }

      const float z = (-b + (high_arc ? std::sqrt(root) : -std::sqrt(root))) / (2.0f * a);
      pitch = std::atan(z);
      horizontal_velocity = (work_speed * std::cos(pitch)) - (work_lift * std::sin(pitch));
      if (horizontal_velocity <= 1.0f) {
        return false;
      }

      flight_time = dx / horizontal_velocity;
      if (profile.drag <= 0.0f) {
        break;
      }

      const float drag_step = std::clamp(profile.drag * flight_time, 0.0f, 0.92f);
      const float prev_speed = work_speed;
      work_speed = std::max(work_speed * (1.0f - drag_step), 200.0f);
      work_lift = std::max(work_lift * (1.0f - drag_step), 0.0f);
      if (std::fabs(work_speed - prev_speed) < 0.35f) {
        break;
      }
    }

    angle.x = -pitch * radpi;
  } else {
    angle = local_prediction_direction_to_angles(local_prediction_normalize(delta));
    flight_time = local_prediction_vector_length(delta) / speed;
  }

  if (!std::isfinite(flight_time) || flight_time <= 0.0f) {
    return false;
  }

  *angle_out = angle;
  *time_out = flight_time;
  return true;
}

inline bool projectile_sim_solve_launch_to_point(Player* localplayer,
  Weapon* weapon,
  const projectile_sim_profile& profile,
  const Vec3& target_position,
  bool high_arc,
  projectile_sim_launch* launch_out,
  float* flight_time_out) {
  if (localplayer == nullptr || weapon == nullptr || launch_out == nullptr || flight_time_out == nullptr || !profile.valid) {
    return false;
  }

  Vec3 shoot_angles{};
  float flight_time = 0.0f;
  if (!projectile_sim_calc_angle_to_point(localplayer->get_shoot_pos(), target_position, profile, high_arc, &shoot_angles, &flight_time)) {
    return false;
  }

  projectile_sim_launch launch{};
  for (int pass = 0; pass < 5; ++pass) {
    launch = projectile_sim_build_launch_from_angles(localplayer, weapon, shoot_angles, profile);

    Vec3 adjusted_target = target_position;
    if (profile.inherit_velocity) {
      adjusted_target -= launch.inherited_velocity * flight_time;
    }

    Vec3 desired_angles{};
    if (!projectile_sim_calc_angle_to_point(launch.origin, adjusted_target, profile, high_arc, &desired_angles, &flight_time)) {
      return false;
    }

    shoot_angles = desired_angles;
  }

  launch = projectile_sim_build_launch_from_angles(localplayer, weapon, shoot_angles, profile);
  Vec3 adjusted_target = target_position;
  if (profile.inherit_velocity) {
    adjusted_target -= launch.inherited_velocity * flight_time;
  }

  if (!projectile_sim_calc_angle_to_point(launch.origin, adjusted_target, profile, high_arc, &shoot_angles, &flight_time)) {
    return false;
  }

  launch = projectile_sim_build_launch_from_angles(localplayer, weapon, shoot_angles, profile);
  *launch_out = launch;
  *flight_time_out = flight_time;
  return true;
}

inline bool projectile_sim_solve_launch_for_time(Player* localplayer,
  Weapon* weapon,
  const projectile_sim_profile& profile,
  const Vec3& target_position,
  float travel_time,
  projectile_sim_launch* launch_out) {
  if (localplayer == nullptr || weapon == nullptr || launch_out == nullptr || !profile.valid || travel_time <= 0.0001f) {
    return false;
  }

  Vec3 shoot_angles = local_prediction_direction_to_angles(local_prediction_normalize(target_position - localplayer->get_shoot_pos()));
  projectile_sim_launch launch = projectile_sim_build_launch_from_angles(localplayer, weapon, shoot_angles, profile);

  for (int pass = 0; pass < 5; ++pass) {
    Vec3 direction = projectile_sim_direction_for_time(launch, profile, target_position, travel_time);
    if (local_prediction_vec3_is_zero(direction)) {
      return false;
    }

    shoot_angles = local_prediction_direction_to_angles(direction);
    launch = projectile_sim_build_launch_from_angles(localplayer, weapon, shoot_angles, profile);
  }

  Vec3 direction = projectile_sim_direction_for_time(launch, profile, target_position, travel_time);
  if (local_prediction_vec3_is_zero(direction)) {
    return false;
  }

  launch.angles = local_prediction_direction_to_angles(direction);
  launch.direction = direction;
  *launch_out = launch;
  return true;
}

inline Vec3 projectile_sim_position_at_trace_time(const projectile_sim_result& sim_result, float time) {
  if (!sim_result.valid || sim_result.steps.empty()) {
    return Vec3{};
  }

  if (time <= sim_result.steps.front().time || sim_result.steps.size() == 1) {
    return sim_result.steps.front().position;
  }

  for (size_t index = 1; index < sim_result.steps.size(); ++index) {
    const projectile_sim_step& previous = sim_result.steps[index - 1];
    const projectile_sim_step& current = sim_result.steps[index];
    if (time > current.time) {
      continue;
    }

    const float duration = std::max(current.time - previous.time, 0.0001f);
    const float fraction = std::clamp((time - previous.time) / duration, 0.0f, 1.0f);
    return previous.position + ((current.position - previous.position) * fraction);
  }

  return sim_result.steps.back().position;
}

inline float projectile_sim_direct_tolerance(const projectile_sim_profile& profile) {
  const float hull_2d = std::sqrt((profile.hull.x * profile.hull.x) + (profile.hull.y * profile.hull.y));
  const float hull_size = std::max(hull_2d, std::max(profile.hull.z, 6.0f));
  return std::clamp(hull_size + 10.0f, 16.0f, 48.0f);
}

inline bool projectile_sim_closest_approach_to_linear_target(const projectile_sim_result& sim_result,
  const Vec3& target_origin,
  const Vec3& target_velocity,
  float* time_out,
  float* miss_out) {
  if (!sim_result.valid || sim_result.steps.empty() || time_out == nullptr || miss_out == nullptr) {
    return false;
  }

  float best_time = sim_result.steps.front().time;
  Vec3 target_position = target_origin + (target_velocity * best_time);
  float best_miss = distance_3d(sim_result.steps.front().position, target_position);

  for (size_t index = 1; index < sim_result.steps.size(); ++index) {
    const projectile_sim_step& previous = sim_result.steps[index - 1];
    const projectile_sim_step& current = sim_result.steps[index];
    const float duration = current.time - previous.time;
    if (duration <= 0.0001f) {
      continue;
    }

    const Vec3 projectile_delta = current.position - previous.position;
    const Vec3 target_start = target_origin + (target_velocity * previous.time);
    const Vec3 target_end = target_origin + (target_velocity * current.time);
    const Vec3 target_delta = target_end - target_start;
    const Vec3 relative_start = previous.position - target_start;
    const Vec3 relative_delta = projectile_delta - target_delta;
    const float relative_delta_sq = local_prediction_dot_3d(relative_delta, relative_delta);
    float segment_t = 0.0f;
    if (relative_delta_sq > 0.0001f) {
      segment_t = std::clamp(-local_prediction_dot_3d(relative_start, relative_delta) / relative_delta_sq, 0.0f, 1.0f);
    }

    const Vec3 relative_position = relative_start + (relative_delta * segment_t);
    const float miss = local_prediction_vector_length(relative_position);
    if (miss < best_miss) {
      best_miss = miss;
      best_time = previous.time + (duration * segment_t);
    }
  }

  *time_out = best_time;
  *miss_out = best_miss;
  return std::isfinite(best_time) && std::isfinite(best_miss);
}

inline bool projectile_sim_closest_approach_to_path(const projectile_sim_result& sim_result,
  const LocalPredictionEntityPath& target_path,
  const Vec3& target_offset,
  float* time_out,
  float* miss_out) {
  if (!sim_result.valid || sim_result.steps.empty() || !target_path.valid || target_path.positions.empty() ||
      time_out == nullptr || miss_out == nullptr) {
    return false;
  }

  const float min_time = std::max(sim_result.steps.front().time, target_path.start_time);
  if (min_time > sim_result.steps.back().time) {
    return false;
  }

  float best_time = min_time;
  Vec3 target_position = local_prediction_path_position_at_time(target_path, best_time) + target_offset;
  float best_miss = distance_3d(projectile_sim_position_at_trace_time(sim_result, best_time), target_position);

  for (size_t index = 1; index < sim_result.steps.size(); ++index) {
    const projectile_sim_step& previous = sim_result.steps[index - 1];
    const projectile_sim_step& current = sim_result.steps[index];
    if (current.time < min_time) {
      continue;
    }

    const float segment_start_time = std::max(previous.time, min_time);
    const float segment_end_time = current.time;
    const float duration = segment_end_time - segment_start_time;
    if (duration <= 0.0001f) {
      continue;
    }

    const Vec3 projectile_start = projectile_sim_position_at_trace_time(sim_result, segment_start_time);
    const Vec3 projectile_end = current.position;
    const Vec3 projectile_delta = projectile_end - projectile_start;
    const Vec3 target_start = local_prediction_path_position_at_time(target_path, segment_start_time) + target_offset;
    const Vec3 target_end = local_prediction_path_position_at_time(target_path, segment_end_time) + target_offset;
    const Vec3 target_delta = target_end - target_start;
    const Vec3 relative_start = projectile_start - target_start;
    const Vec3 relative_delta = projectile_delta - target_delta;
    const float relative_delta_sq = local_prediction_dot_3d(relative_delta, relative_delta);
    float segment_t = 0.0f;
    if (relative_delta_sq > 0.0001f) {
      segment_t = std::clamp(-local_prediction_dot_3d(relative_start, relative_delta) / relative_delta_sq, 0.0f, 1.0f);
    }

    const Vec3 relative_position = relative_start + (relative_delta * segment_t);
    const float miss = local_prediction_vector_length(relative_position);
    if (miss < best_miss) {
      best_miss = miss;
      best_time = segment_start_time + (duration * segment_t);
    }
  }

  *time_out = best_time;
  *miss_out = best_miss;
  return std::isfinite(best_time) && std::isfinite(best_miss);
}

inline bool projectile_sim_closest_approach_to_static_point(const projectile_sim_result& sim_result,
  const Vec3& target_origin,
  float* time_out,
  float* miss_out) {
  if (!sim_result.valid || sim_result.steps.empty() || time_out == nullptr || miss_out == nullptr) {
    return false;
  }

  float best_time = sim_result.steps.front().time;
  float best_miss = distance_3d(sim_result.steps.front().position, target_origin);
  for (size_t index = 1; index < sim_result.steps.size(); ++index) {
    const projectile_sim_step& previous = sim_result.steps[index - 1];
    const projectile_sim_step& current = sim_result.steps[index];
    const Vec3 segment = current.position - previous.position;
    const float segment_length_sq = local_prediction_dot_3d(segment, segment);
    float segment_t = 0.0f;
    if (segment_length_sq > 0.0001f) {
      segment_t = std::clamp(local_prediction_dot_3d(target_origin - previous.position, segment) / segment_length_sq, 0.0f, 1.0f);
    }

    const Vec3 closest_position = previous.position + (segment * segment_t);
    const float miss = distance_3d(closest_position, target_origin);
    if (miss < best_miss) {
      best_miss = miss;
      best_time = previous.time + ((current.time - previous.time) * segment_t);
    }
  }

  *time_out = best_time;
  *miss_out = best_miss;
  return std::isfinite(best_time) && std::isfinite(best_miss);
}

inline LocalPredictionProjectileTrace local_prediction_trace_from_projectile_sim(const projectile_sim_result& sim_result) {
  LocalPredictionProjectileTrace trace{};
  if (!sim_result.valid || sim_result.steps.empty()) {
    return trace;
  }

  trace.steps.reserve(sim_result.steps.size());
  for (const projectile_sim_step& sim_step : sim_result.steps) {
    trace.steps.push_back({
      .time = sim_step.time,
      .position = sim_step.position,
      .velocity = sim_step.velocity
    });
  }
  trace.valid = !trace.steps.empty();
  return trace;
}

inline int local_prediction_projectile_arc_branch_count(bool has_gravity) {
  if (!has_gravity) {
    return 1;
  }

  return 2;
}

inline bool local_prediction_projectile_arc_high_branch(bool has_gravity, int branch_index) {
  return has_gravity && branch_index == 1;
}

inline float local_prediction_projectile_arc_score_bias(const projectile_sim_profile& profile,
  bool high_arc,
  float flight_time) {
  if (!high_arc) {
    return 0.0f;
  }

  // gentle preference for low-arc shots; must stay well below intercept_error_cap so high-arc
  // intercepts are still accepted when the low-arc solver fails (long-range huntsman / grenades).
  // intercept_error_cap is roughly max(320, speed*time_step*20), so cap the bias at ~25% of the
  // expected speed-based component plus a small extra cost proportional to flight time.
  const float speed_component = std::clamp(profile.params.speed * profile.params.time_step * 20.0f, 320.0f, 1200.0f);
  const float base_cost = std::min(speed_component * 0.18f, 90.0f);
  const float time_cost = std::min(
    std::clamp(flight_time, 0.0f, profile.params.max_time) * profile.params.speed * 0.04f,
    60.0f);
  return base_cost + time_cost;
}

inline LocalPredictionInterceptResult local_prediction_find_projectile_intercept(Player* localplayer,
  Weapon* weapon,
  const LocalPredictionEntityPath& target_path,
  const Vec3& target_offset,
  user_cmd* user_cmd,
  float horizon_seconds = 1.5f) {
  LocalPredictionInterceptResult result{};
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr || !target_path.valid || target_path.positions.empty()) return result;

  projectile_sim_profile profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  if (!profile.valid || profile.params.speed <= 0.0f) return result;
  profile.params.max_time = std::min(profile.params.max_time, horizon_seconds);
  profile.lifetime = profile.params.max_time;

  float best_error = FLT_MAX;
  float best_time = 0.0f;
  float best_distance = FLT_MAX;
  projectile_sim_launch best_launch{};
  size_t best_path_index = 0;

  const size_t path_step_count = std::min(
    target_path.positions.size() - 1,
    static_cast<size_t>(std::clamp(config.aimbot.projectile_prediction_ticks, 8, 420)));
  size_t path_stride = std::max<size_t>(1, (path_step_count + 47) / 48);
  if (proj_aim_budget().active) {
    path_stride *= static_cast<size_t>(std::clamp(proj_aim_budget().intercept_path_stride_mul, 1, 8));
  }

  const auto score_path_index = [&](size_t path_index) {
    const float target_time = target_path.start_time + (static_cast<float>(path_index) * target_path.time_step);
    if (target_time <= 0.0001f) {
      return;
    }
    if (target_time > profile.params.max_time) {
      return;
    }
    const Vec3 predicted_target = target_path.positions[path_index] + target_offset;

    const Vec3 to_target = predicted_target - localplayer->get_shoot_pos();
    const float straight_distance = local_prediction_vector_length(to_target);
    if (straight_distance <= 1.0f) {
      return;
    }

    const int arc_branch_count = local_prediction_projectile_arc_branch_count(profile.params.gravity > 0.0f);
    for (int arc_branch = 0; arc_branch < arc_branch_count; ++arc_branch) {
      const bool high_arc = local_prediction_projectile_arc_high_branch(profile.params.gravity > 0.0f, arc_branch);
      projectile_sim_launch candidate_launch{};
      float flight_time = 0.0f;
      if (!projectile_sim_solve_launch_to_point(
          localplayer,
          weapon,
          profile,
          predicted_target,
          high_arc,
          &candidate_launch,
          &flight_time)) {
        continue;
      }

      if (flight_time > profile.params.max_time) {
        continue;
      }

      const float spatial_error = distance_3d(projectile_sim_position_at_time(candidate_launch, profile, flight_time), predicted_target);
      const float time_error = std::fabs(flight_time - target_time);
      const float arc_bias = local_prediction_projectile_arc_score_bias(profile, high_arc, flight_time);
      const float score_error = spatial_error + (time_error * profile.params.speed) + arc_bias;
      if (score_error < best_error) {
        best_error = score_error;
        best_time = flight_time;
        best_distance = straight_distance;
        best_launch = candidate_launch;
        best_path_index = path_index;
      }
    }
  };

  for (size_t path_index = 0; path_index <= path_step_count; path_index += path_stride) {
    score_path_index(path_index);

    if (path_step_count - path_index < path_stride) {
      break;
    }
  }

  if (path_stride > 1) {
    const size_t back = std::min(path_stride * 5, best_path_index);
    const size_t start_refine = best_path_index > back ? best_path_index - back : 0;
    const size_t end_refine = std::min(path_step_count, best_path_index + (path_stride * 5));
    for (size_t path_index = start_refine; path_index <= end_refine; ++path_index) {
      if ((path_index % path_stride) == 0 && path_index != best_path_index) {
        continue;
      }
      score_path_index(path_index);
    }

    const size_t fine_back = std::min<size_t>(2, best_path_index);
    const size_t fine_start = best_path_index > fine_back ? best_path_index - fine_back : 0;
    const size_t fine_end = std::min(path_step_count, best_path_index + 2);
    for (size_t path_index = fine_start; path_index <= fine_end; ++path_index) {
      if (path_index == best_path_index) {
        continue;
      }
      score_path_index(path_index);
    }
  }

  const float intercept_error_cap =
    std::max(320.0f, profile.params.speed * profile.params.time_step * 20.0f) +
    (proj_aim_budget().active ? proj_aim_budget().intercept_error_cap_add : 0.0f);
  if (best_error > intercept_error_cap || local_prediction_vec3_is_zero(best_launch.direction)) {
    return {};
  }

  profile.lifetime = std::min(profile.lifetime, best_time + profile.params.time_step);
  const projectile_sim_result sim_result = projectile_sim_run(best_launch, profile);
  LocalPredictionProjectileTrace trace = local_prediction_trace_from_projectile_sim(sim_result);
  if (!sim_result.valid || !trace.valid || trace.steps.empty()) {
    return {};
  }
  float final_time = 0.0f;
  float final_miss = FLT_MAX;
  if (!projectile_sim_closest_approach_to_path(sim_result, target_path, target_offset, &final_time, &final_miss)) {
    return {};
  }

  const Vec3 final_base_origin = local_prediction_path_position_at_time(target_path, final_time);
  const Vec3 final_target = final_base_origin + target_offset;
  // final-miss tolerance: the projectile_sim_direct_tolerance is point-to-point. proj_aim_trace
  // does the precise hull-vs-path test downstream, so here we add the player-hull horizontal
  // radius (~24) so we don't reject an intercept that proj_aim_trace would happily accept.
  constexpr float player_hull_extra = 24.0f;
  if (final_miss > projectile_sim_direct_tolerance(profile) + player_hull_extra) {
    return {};
  }

  result.valid = true;
  result.has_target_base_origin = true;
  result.intercept_time = final_time;
  result.intercept_distance = best_distance;
  result.miss_distance = final_miss;
  result.aim_angles = best_launch.angles;
  result.target_origin = final_target;
  result.target_base_origin = final_base_origin;
  result.target_offset = target_offset;
  result.target_velocity = target_path.final_velocity;
  result.trace = trace;
  return result;
}

inline LocalPredictionInterceptResult local_prediction_find_static_projectile_intercept(Player* localplayer,
  Weapon* weapon,
  const Vec3& target_origin,
  user_cmd* user_cmd,
  float horizon_seconds = 1.5f) {
  LocalPredictionInterceptResult result{};
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr) return result;

  projectile_sim_profile profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  if (!profile.valid || profile.params.speed <= 0.0f) return result;
  profile.params.max_time = std::min(profile.params.max_time, horizon_seconds);
  profile.lifetime = profile.params.max_time;

  float best_score = FLT_MAX;
  float best_time = 0.0f;
  float best_miss = FLT_MAX;
  projectile_sim_launch best_launch{};
  projectile_sim_result best_sim_result{};

  const int arc_branch_count = local_prediction_projectile_arc_branch_count(profile.params.gravity > 0.0f);
  for (int arc_branch = 0; arc_branch < arc_branch_count; ++arc_branch) {
    const bool high_arc = local_prediction_projectile_arc_high_branch(profile.params.gravity > 0.0f, arc_branch);
    projectile_sim_launch candidate_launch{};
    float flight_time = 0.0f;
    if (!projectile_sim_solve_launch_to_point(
        localplayer,
        weapon,
        profile,
        target_origin,
        high_arc,
        &candidate_launch,
        &flight_time)) {
      continue;
    }

    if (!candidate_launch.valid || flight_time > profile.params.max_time) {
      continue;
    }

    projectile_sim_profile sim_profile = profile;
    sim_profile.lifetime = std::min(sim_profile.lifetime, flight_time + sim_profile.params.time_step);
    const projectile_sim_result sim_result = projectile_sim_run(candidate_launch, sim_profile);
    if (!sim_result.valid || sim_result.steps.empty()) {
      continue;
    }

    float final_time = 0.0f;
    float final_miss = FLT_MAX;
    if (!projectile_sim_closest_approach_to_static_point(sim_result, target_origin, &final_time, &final_miss)) {
      continue;
    }

    const float arc_bias = local_prediction_projectile_arc_score_bias(profile, high_arc, flight_time);
    const float score = final_miss + (std::fabs(final_time - flight_time) * profile.params.speed) + arc_bias;
    if (score < best_score) {
      best_score = score;
      best_time = final_time;
      best_miss = final_miss;
      best_launch = candidate_launch;
      best_sim_result = sim_result;
    }
  }

  if (!best_launch.valid || best_miss > projectile_sim_direct_tolerance(profile)) {
    return {};
  }

  LocalPredictionProjectileTrace trace = local_prediction_trace_from_projectile_sim(best_sim_result);
  if (!trace.valid || trace.steps.empty()) {
    return {};
  }

  result.valid = true;
  result.has_target_base_origin = true;
  result.intercept_time = best_time;
  result.intercept_distance = distance_3d(localplayer->get_shoot_pos(), target_origin);
  result.miss_distance = best_miss;
  result.aim_angles = best_launch.angles;
  result.target_origin = target_origin;
  result.target_base_origin = target_origin;
  result.target_offset = Vec3{};
  result.target_velocity = Vec3{};
  result.trace = trace;
  return result;
}

inline LocalPredictionInterceptResult local_prediction_find_projectile_intercept(Player* localplayer,
  Weapon* weapon,
  const Vec3& target_origin,
  const Vec3& target_velocity,
  user_cmd* user_cmd,
  float horizon_seconds = 1.5f) {
  LocalPredictionInterceptResult result{};
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr) return result;

  if (local_prediction_vector_length(target_velocity) <= 0.001f) {
    return local_prediction_find_static_projectile_intercept(
      localplayer,
      weapon,
      target_origin,
      user_cmd,
      horizon_seconds);
  }

  projectile_sim_profile profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  if (!profile.valid || profile.params.speed <= 0.0f) return result;
  profile.params.max_time = std::min(profile.params.max_time, horizon_seconds);
  profile.lifetime = profile.params.max_time;

  float best_error = FLT_MAX;
  float best_time = 0.0f;
  float best_distance = FLT_MAX;
  projectile_sim_launch best_launch{};

  const int step_count = std::clamp(
    static_cast<int>(std::ceil(profile.params.max_time / profile.params.time_step)),
    4,
    std::clamp(config.aimbot.projectile_prediction_ticks, 8, 420));
  int step_stride = std::max(1, (step_count + 47) / 48);
  if (proj_aim_budget().active) {
    step_stride *= std::clamp(proj_aim_budget().intercept_path_stride_mul, 1, 8);
  }
  for (int step_index = 1; step_index <= step_count; step_index += step_stride) {
    const float target_time = std::min(profile.params.max_time, step_index * profile.params.time_step);
    Vec3 predicted_target = target_origin + (target_velocity * target_time);

    Vec3 to_target = predicted_target - localplayer->get_shoot_pos();
    float straight_distance = local_prediction_vector_length(to_target);
    if (straight_distance <= 1.0f) continue;

    const int arc_branch_count = local_prediction_projectile_arc_branch_count(profile.params.gravity > 0.0f);
    for (int arc_branch = 0; arc_branch < arc_branch_count; ++arc_branch) {
      const bool high_arc = local_prediction_projectile_arc_high_branch(profile.params.gravity > 0.0f, arc_branch);
      projectile_sim_launch candidate_launch{};
      float flight_time = 0.0f;
      if (!projectile_sim_solve_launch_to_point(
          localplayer,
          weapon,
          profile,
          predicted_target,
          high_arc,
          &candidate_launch,
          &flight_time)) {
        continue;
      }

      if (flight_time > profile.params.max_time) {
        continue;
      }

      const float spatial_error = distance_3d(projectile_sim_position_at_time(candidate_launch, profile, flight_time), predicted_target);
      const float time_error = std::fabs(flight_time - target_time);
      const float arc_bias = local_prediction_projectile_arc_score_bias(profile, high_arc, flight_time);
      const float score_error = spatial_error + (time_error * profile.params.speed) + arc_bias;
      if (score_error < best_error) {
        best_error = score_error;
        best_time = flight_time;
        best_distance = straight_distance;
        best_launch = candidate_launch;
      }
    }
  }

  const float intercept_error_cap =
    std::max(320.0f, profile.params.speed * profile.params.time_step * 20.0f) +
    (proj_aim_budget().active ? proj_aim_budget().intercept_error_cap_add : 0.0f);
  if (best_error > intercept_error_cap || local_prediction_vec3_is_zero(best_launch.direction)) {
    return {};
  }

  profile.lifetime = std::min(profile.lifetime, best_time + profile.params.time_step);
  const projectile_sim_result sim_result = projectile_sim_run(best_launch, profile);
  LocalPredictionProjectileTrace trace = local_prediction_trace_from_projectile_sim(sim_result);
  if (!sim_result.valid || !trace.valid || trace.steps.empty()) {
    return {};
  }
  float final_time = 0.0f;
  float final_miss = FLT_MAX;
  if (!projectile_sim_closest_approach_to_linear_target(sim_result, target_origin, target_velocity, &final_time, &final_miss)) {
    return {};
  }

  const Vec3 final_target = target_origin + (target_velocity * final_time);
  constexpr float player_hull_extra = 24.0f;
  if (final_miss > projectile_sim_direct_tolerance(profile) + player_hull_extra) {
    return {};
  }

  result.valid = true;
  result.has_target_base_origin = true;
  result.intercept_time = final_time;
  result.intercept_distance = best_distance;
  result.miss_distance = final_miss;
  result.aim_angles = best_launch.angles;
  result.target_origin = final_target;
  result.target_base_origin = final_target;
  result.target_offset = Vec3{};
  result.target_velocity = target_velocity;
  result.trace = trace;
  return result;
}

inline LocalPredictionInterceptResult local_prediction_find_projectile_intercept(Player* localplayer,
  Weapon* weapon,
  Entity* target,
  user_cmd* user_cmd,
  float horizon_seconds = 1.5f) {
  LocalPredictionInterceptResult result{};
  if (localplayer == nullptr || weapon == nullptr || target == nullptr || user_cmd == nullptr) return result;

  Vec3 target_velocity = local_prediction_estimate_entity_velocity(target);
  if (local_prediction_vector_length(target_velocity) <= 0.001f && target->get_class_id() == class_id::PLAYER) {
    target_velocity = static_cast<Player*>(target)->get_velocity();
  }

  return local_prediction_find_projectile_intercept(
    localplayer,
    weapon,
    target->get_origin(),
    target_velocity,
    user_cmd,
    horizon_seconds);
}

namespace projectile_sim {

using profile = projectile_sim_profile;
using launch = projectile_sim_launch;
using step = projectile_sim_step;
using result = projectile_sim_result;
using simulation = projectile_simulation;
using trace_mode = projectile_sim_trace_mode;

inline profile profile_for_weapon(Player* localplayer, Weapon* weapon) {
  return projectile_sim_profile_for_weapon(localplayer, weapon);
}

inline launch build_launch(Player* localplayer, Weapon* weapon, user_cmd* cmd, const profile& sim_profile) {
  return projectile_sim_build_launch(localplayer, weapon, cmd, sim_profile);
}

inline launch build_launch_from_angles(Player* localplayer, Weapon* weapon, const Vec3& angles, const profile& sim_profile) {
  return projectile_sim_build_launch_from_angles(localplayer, weapon, angles, sim_profile);
}

inline result run(const launch& sim_launch,
  const profile& sim_profile,
  Entity* skip_entity = nullptr,
  Entity* target_entity = nullptr,
  trace_mode mode = trace_mode::none) {
  return projectile_sim_run(sim_launch, sim_profile, skip_entity, target_entity, mode);
}

}
#endif
