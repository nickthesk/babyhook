/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_controller.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "features/automation/navbot/navbot_controller.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

#include "imgui/dearimgui.hpp"

#include "core/entity_cache.hpp"
#include "core/math/math.hpp"

#include "features/automation/medic_automation/medic_automation.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/menu/config.hpp"

#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/team_objective_resource.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"

namespace navbot
{

namespace
{

navbot_controller* global_controller = nullptr;
constexpr float goal_refresh_interval = 1.0f;
constexpr float goal_retry_interval = 0.2f;
constexpr float path_retry_interval = 1.0f;
constexpr float weapon_switch_interval = 0.35f;
constexpr float navbot_throwable_look_suppress_seconds = 0.55f;
constexpr int team_unassigned = 0;
constexpr int tf_team_blue_value = 3;
constexpr int gr_state_init = 0;
constexpr int gr_state_preround = 3;
constexpr int gr_state_team_win = 5;
constexpr int gr_num_round_states = 11;
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE
constexpr bool textmode_build = true;
constexpr float hazard_refresh_interval = 1.0f;
#else
constexpr bool textmode_build = false;
constexpr float hazard_refresh_interval = 0.25f;
#endif
static float g_navbot_throwable_look_suppress_until = -1.0e9f;

path_clearance path_clearance_for_player(Player* localplayer)
{
  path_clearance clearance{};
  if (localplayer == nullptr)
  {
    return clearance;
  }

  const bool ducking = localplayer->is_ducking();
  const Vec3 mins = localplayer->get_player_mins(ducking);
  const Vec3 maxs = localplayer->get_player_maxs(ducking);
  const float width_x = maxs.x - mins.x;
  const float width_y = maxs.y - mins.y;
  const float sampled_width = std::max(width_x, width_y);
  if (std::isfinite(sampled_width) && sampled_width > 1.0f)
  {
    clearance.width = std::max(sampled_width, player_width);
  }

  const float half_width_x = std::max(std::fabs(mins.x), std::fabs(maxs.x));
  const float half_width_y = std::max(std::fabs(mins.y), std::fabs(maxs.y));
  const float sampled_half_width = std::max(half_width_x, half_width_y);
  if (std::isfinite(sampled_half_width) && sampled_half_width > 1.0f)
  {
    clearance.half_width = std::max(sampled_half_width, clearance.width * 0.5f);
  }
  else
  {
    clearance.half_width = clearance.width * 0.5f;
  }

  return clearance;
}

bool navbot_weapon_is_arc_throwable(Weapon* weapon)
{
  if (weapon == nullptr)
  {
    return false;
  }

  switch (weapon->get_def_id())
  {
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return true;
  default:
    return false;
  }
}

void navbot_update_throwable_look_suppress(Weapon* weapon, user_cmd* user_cmd, float current_time)
{
  if (user_cmd == nullptr)
  {
    return;
  }

  if ((user_cmd->buttons & IN_ATTACK2) != 0 && navbot_weapon_is_arc_throwable(weapon))
  {
    g_navbot_throwable_look_suppress_until = std::max(
      g_navbot_throwable_look_suppress_until,
      current_time + navbot_throwable_look_suppress_seconds);
  }
}

bool navbot_throwable_look_suppresses_path_look(float current_time)
{
  return current_time < g_navbot_throwable_look_suppress_until;
}

bool round_state_is_warmup(int round_state)
{
  return round_state >= gr_state_init && round_state < gr_state_preround;
}

bool round_state_is_valid(int round_state)
{
  return round_state >= gr_state_init && round_state < gr_num_round_states;
}

bool round_state_has_started(int round_state)
{
  return round_state >= gr_state_preround && round_state < gr_state_team_win;
}

enum class navbot_weapon_slot
{
  none = 0,
  primary = 1,
  secondary = 2,
  melee = 3
};

bool goal_is_supply(goal_type type)
{
  return type == goal_type::get_health || type == goal_type::get_ammo;
}

bool goal_is_reload(goal_type type)
{
  return type == goal_type::reload_weapons;
}

bool goal_is_combat(goal_type type)
{
  return type == goal_type::hold_range_on_enemy
    || type == goal_type::melee_chase
    || type == goal_type::sentry_snipe;
}

bool goal_is_payload(goal_type type)
{
  return type == goal_type::push_payload || type == goal_type::defend_payload;
}

float goal_destination_shift_sq(const navbot_goal_state& left, const navbot_goal_state& right)
{
  auto dx = left.goal.destination.x - right.goal.destination.x;
  auto dy = left.goal.destination.y - right.goal.destination.y;
  auto dz = left.goal.destination.z - right.goal.destination.z;
  return dx * dx + dy * dy + dz * dz;
}

float destination_reach_distance_for_goal(goal_type type)
{
  if (goal_is_supply(type))
  {
    return pickup_destination_reach_distance;
  }
  if (type == goal_type::push_payload)
  {
    return 45.0f;
  }
  if (type == goal_type::reload_weapons)
  {
    return 90.0f;
  }
  if (type == goal_type::heal_follow)
  {
    return 170.0f;
  }
  if (type == goal_type::defend_payload)
  {
    return 90.0f;
  }
  if (type == goal_type::capture_objective)
  {
    return 30.0f;
  }

  return crumb_reach_distance;
}

bool reload_job_still_needed(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return false;
  }

  auto* weapon = localplayer->get_weapon();
  return weapon != nullptr && weapon->get_clip1() == 0;
}

bool supply_goal_still_needed(goal_type type, Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return false;
  }

  if (type == goal_type::get_health)
  {
    auto max_health = localplayer->get_max_health();
    if (max_health <= 0)
    {
      return false;
    }

    auto health_ratio = static_cast<float>(localplayer->get_health()) / static_cast<float>(max_health);
    return health_ratio < 0.88f;
  }

  if (type == goal_type::get_ammo)
  {
    auto* weapon = localplayer->get_weapon();
    return weapon != nullptr && weapon->get_clip1() <= 0;
  }

  return false;
}

bool map_has_cp_or_pl_prefix(const std::string& map_name)
{
  return map_name.starts_with("cp_") || map_name.starts_with("pl_") || map_name.starts_with("plr_");
}

navbot_weapon_slot weapon_slot_for_type(int type_id, tf_class class_type)
{
  switch (type_id)
  {
    case TF_WEAPON_BAT:
    case TF_WEAPON_BAT_WOOD:
    case TF_WEAPON_BOTTLE:
    case TF_WEAPON_FIREAXE:
    case TF_WEAPON_CLUB:
    case TF_WEAPON_CROWBAR:
    case TF_WEAPON_KNIFE:
    case TF_WEAPON_FISTS:
    case TF_WEAPON_SHOVEL:
    case TF_WEAPON_WRENCH:
    case TF_WEAPON_BONESAW:
    case TF_WEAPON_SWORD:
    case TF_WEAPON_BAT_FISH:
    case TF_WEAPON_BAT_GIFTWRAP:
    case TF_WEAPON_STICKBOMB:
    case TF_WEAPON_HARVESTER_SAW:
      return navbot_weapon_slot::melee;
    case TF_WEAPON_SCATTERGUN:
    case TF_WEAPON_SNIPERRIFLE:
    case TF_WEAPON_MINIGUN:
    case TF_WEAPON_SYRINGEGUN_MEDIC:
    case TF_WEAPON_ROCKETLAUNCHER:
    case TF_WEAPON_GRENADELAUNCHER:
    case TF_WEAPON_FLAMETHROWER:
    case TF_WEAPON_REVOLVER:
    case TF_WEAPON_SHOTGUN_PRIMARY:
    case TF_WEAPON_SHOTGUN_SOLDIER:
    case TF_WEAPON_SHOTGUN_HWG:
    case TF_WEAPON_SHOTGUN_PYRO:
    case TF_WEAPON_COMPOUND_BOW:
    case TF_WEAPON_HANDGUN_SCOUT_PRIMARY:
    case TF_WEAPON_CROSSBOW:
    case TF_WEAPON_SODA_POPPER:
    case TF_WEAPON_SNIPERRIFLE_DECAP:
    case TF_WEAPON_PARTICLE_CANNON:
    case TF_WEAPON_DRG_POMSON:
    case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
    case TF_WEAPON_CANNON:
    case TF_WEAPON_SNIPERRIFLE_CLASSIC:
    case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
      return navbot_weapon_slot::primary;
    case TF_WEAPON_PIPEBOMBLAUNCHER:
    case TF_WEAPON_PISTOL:
    case TF_WEAPON_PISTOL_SCOUT:
    case TF_WEAPON_SMG:
    case TF_WEAPON_MEDIGUN:
    case TF_WEAPON_INVIS:
    case TF_WEAPON_FLAREGUN:
    case TF_WEAPON_LUNCHBOX:
    case TF_WEAPON_JAR:
    case TF_WEAPON_BUFF_ITEM:
    case TF_WEAPON_LASER_POINTER:
    case TF_WEAPON_SENTRY_REVENGE:
    case TF_WEAPON_JAR_MILK:
    case TF_WEAPON_HANDGUN_SCOUT_SECONDARY:
    case TF_WEAPON_RAYGUN:
    case TF_WEAPON_MECHANICAL_ARM:
    case TF_WEAPON_FLAREGUN_REVENGE:
    case TF_WEAPON_CLEAVER:
    case TF_WEAPON_THROWABLE:
    case TF_WEAPON_PARACHUTE:
      return navbot_weapon_slot::secondary;
    default:
      break;
  }

  switch (class_type)
  {
    case tf_class::MEDIC:
      return navbot_weapon_slot::secondary;
    case tf_class::SPY:
      return navbot_weapon_slot::primary;
    default:
      return navbot_weapon_slot::primary;
  }
}

navbot_weapon_slot weapon_slot_for(Weapon* weapon, tf_class class_type)
{
  if (weapon == nullptr)
  {
    return navbot_weapon_slot::none;
  }

  if (weapon->is_melee())
  {
    return navbot_weapon_slot::melee;
  }

  if (weapon->is_sniper_rifle() || weapon->is_minigun())
  {
    return navbot_weapon_slot::primary;
  }

  switch (weapon->get_def_id())
  {
    case Scout_m_Scattergun:
    case Scout_m_ScattergunR:
    case Scout_m_ForceANature:
    case Scout_m_TheShortstop:
    case Scout_m_TheSodaPopper:
    case Scout_m_FestiveScattergun:
    case Scout_m_BabyFacesBlaster:
    case Scout_m_TheBackScatter:
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
    case Pyro_m_FlameThrower:
    case Pyro_m_FlameThrowerR:
    case Pyro_m_TheBackburner:
    case Pyro_m_TheDegreaser:
    case Pyro_m_ThePhlogistinator:
    case Pyro_m_FestiveFlameThrower:
    case Pyro_m_TheRainblower:
    case Pyro_m_FestiveBackburner:
    case Pyro_m_DragonsFury:
    case Demoman_m_GrenadeLauncher:
    case Demoman_m_GrenadeLauncherR:
    case Demoman_m_TheLochnLoad:
    case Demoman_m_TheLooseCannon:
    case Demoman_m_FestiveGrenadeLauncher:
    case Demoman_m_TheIronBomber:
    case Medic_m_SyringeGun:
    case Medic_m_SyringeGunR:
    case Medic_m_TheBlutsauger:
    case Medic_m_CrusadersCrossbow:
    case Medic_m_TheOverdose:
    case Medic_m_FestiveCrusadersCrossbow:
    case Spy_m_Revolver:
    case Spy_m_RevolverR:
    case Spy_m_TheAmbassador:
    case Spy_m_BigKill:
    case Spy_m_LEtranger:
    case Spy_m_TheEnforcer:
    case Spy_m_TheDiamondback:
    case Spy_m_FestiveAmbassador:
    case Spy_m_FestiveRevolver:
      return navbot_weapon_slot::primary;

    case Scout_s_ScoutsPistol:
    case Scout_s_PistolR:
    case Scout_s_BonkAtomicPunch:
    case Scout_s_VintageLugermorph:
    case Scout_s_CritaCola:
    case Scout_s_MadMilk:
    case Scout_s_Lugermorph:
    case Scout_s_TheWinger:
    case Scout_s_PrettyBoysPocketPistol:
    case Scout_s_TheFlyingGuillotine:
    case Scout_s_TheFlyingGuillotineG:
    case Scout_s_MutatedMilk:
    case Scout_s_FestiveBonk:
    case Pyro_s_TheFlareGun:
    case Pyro_s_TheDetonator:
    case Pyro_s_TheReserveShooter:
    case Pyro_s_TheManmelter:
    case Pyro_s_TheScorchShot:
    case Pyro_s_FestiveFlareGun:
    case Pyro_s_ThermalThruster:
    case Pyro_s_GasPasser:
    case Heavy_s_Sandvich:
    case Heavy_s_TheBuffaloSteakSandvich:
    case Heavy_s_Fishcake:
    case Heavy_s_SecondBanana:
    case Medic_s_MediGun:
    case Medic_s_TheQuickFix:
    case Medic_s_TheVaccinator:
    case Sniper_s_SMG:
    case Sniper_s_SMGR:
    case Sniper_s_TheCleanersCarbine:
    case Sniper_s_DarwinsDangerShield:
    case Sniper_s_Jarate:
    case Sniper_s_TheSelfAwareBeautyMark:
    case Sniper_s_CozyCamper:
    case Spy_w_InvisWatch:
    case Spy_w_InvisWatchR:
    case Spy_w_TheDeadRinger:
    case Spy_w_TheCloakandDagger:
    case Spy_w_EnthusiastsTimepiece:
    case Spy_w_TheQuackenbirdt:
      return navbot_weapon_slot::secondary;
  }

  switch (class_type)
  {
    case tf_class::MEDIC:
      return navbot_weapon_slot::secondary;
    case tf_class::SPY:
      return navbot_weapon_slot::primary;
    default:
      return navbot_weapon_slot::primary;
  }
}

bool weapon_slot_available(Player* localplayer, navbot_weapon_slot slot)
{
  if (localplayer == nullptr || slot == navbot_weapon_slot::none)
  {
    return false;
  }

  auto class_type = localplayer->get_tf_class();
  for (int index = 0; index < Player::max_weapon_count; ++index)
  {
    auto* weapon = localplayer->get_weapon_at(index);
    if (weapon == nullptr)
    {
      continue;
    }

    if (weapon_slot_for(weapon, class_type) == slot)
    {
      return true;
    }
  }

  return false;
}

bool weapon_slot_loaded(Player* localplayer, navbot_weapon_slot slot)
{
  if (localplayer == nullptr || slot == navbot_weapon_slot::none)
  {
    return false;
  }

  auto class_type = localplayer->get_tf_class();
  for (int index = 0; index < Player::max_weapon_count; ++index)
  {
    auto* weapon = localplayer->get_weapon_at(index);
    if (weapon == nullptr || weapon_slot_for(weapon, class_type) != slot)
    {
      continue;
    }

    auto clip = weapon->get_clip1();
    if (clip != 0 || weapon->is_melee())
    {
      return true;
    }
  }

  return false;
}

float distance_to_enemy(Player* localplayer, Player* enemy)
{
  if (localplayer == nullptr || enemy == nullptr)
  {
    return 8192.0f;
  }

  return distance_3d(localplayer->get_origin(), enemy->get_origin());
}

Player* choose_navbot_enemy(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return nullptr;
  }

  Player* aimbot_target = aimbot::active_target_player();
  if (aimbot_target != nullptr
    && !aimbot_target->is_dormant()
    && aimbot_target->is_alive()
    && aimbot_target->get_team() != localplayer->get_team())
  {
    return aimbot_target;
  }

  Player* best_enemy = nullptr;
  auto best_distance = std::numeric_limits<float>::max();
  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (player == nullptr || player == localplayer || player->is_dormant())
    {
      continue;
    }

    if (player->get_team() == localplayer->get_team() || !player->is_alive())
    {
      continue;
    }

    auto distance = distance_3d(localplayer->get_origin(), player->get_origin());
    if (distance < best_distance)
    {
      best_distance = distance;
      best_enemy = player;
    }
  }

  return best_enemy;
}

navbot_weapon_slot choose_default_slot(tf_class class_type)
{
  switch (class_type)
  {
    case tf_class::MEDIC:
      return navbot_weapon_slot::secondary;
    default:
      return navbot_weapon_slot::primary;
  }
}

navbot_weapon_slot choose_combat_slot(Player* localplayer, goal_type goal, Player* enemy)
{
  if (localplayer == nullptr)
  {
    return navbot_weapon_slot::none;
  }

  if (goal == goal_type::engineer_build || goal == goal_type::engineer_maintain)
  {
    return navbot_weapon_slot::melee;
  }

  auto enemy_distance = distance_to_enemy(localplayer, enemy);
  switch (localplayer->get_tf_class())
  {
    case tf_class::SNIPER:
      if (enemy_distance <= 150.0f)
      {
        return navbot_weapon_slot::melee;
      }
      if (enemy_distance <= 425.0f)
      {
        return navbot_weapon_slot::secondary;
      }
      return navbot_weapon_slot::primary;
    case tf_class::SOLDIER:
    case tf_class::DEMOMAN:
      if (enemy_distance <= 225.0f)
      {
        return navbot_weapon_slot::secondary;
      }
      return navbot_weapon_slot::primary;
    case tf_class::PYRO:
      if (enemy_distance <= 360.0f)
      {
        return navbot_weapon_slot::primary;
      }
      return navbot_weapon_slot::secondary;
    case tf_class::MEDIC:
      if (goal_is_combat(goal))
      {
        if (enemy_distance <= 120.0f)
        {
          return navbot_weapon_slot::melee;
        }
        return navbot_weapon_slot::primary;
      }
      return navbot_weapon_slot::secondary;
    case tf_class::SPY:
      if (enemy_distance <= 110.0f)
      {
        return navbot_weapon_slot::melee;
      }
      return navbot_weapon_slot::primary;
    case tf_class::ENGINEER:
      if (goal == goal_type::sentry_snipe)
      {
        return navbot_weapon_slot::primary;
      }
      if (enemy_distance <= 260.0f)
      {
        return navbot_weapon_slot::primary;
      }
      return navbot_weapon_slot::secondary;
    case tf_class::SCOUT:
    case tf_class::HEAVYWEAPONS:
    case tf_class::UNDEFINED:
    default:
      return navbot_weapon_slot::primary;
  }
}

navbot_weapon_slot choose_navbot_weapon_slot(Player* localplayer, const navbot_goal_state& goal_state)
{
  if (localplayer == nullptr)
  {
    return navbot_weapon_slot::none;
  }

  auto goal = goal_state.valid ? goal_state.goal.type : goal_type::roam;
  if (goal == goal_type::engineer_build || goal == goal_type::engineer_maintain)
  {
    if (weapon_slot_available(localplayer, navbot_weapon_slot::melee))
    {
      return navbot_weapon_slot::melee;
    }
  }

  if (goal == goal_type::heal_follow && localplayer->get_tf_class() == tf_class::MEDIC)
  {
    return medic_automation::controller().wants_crossbow()
      ? navbot_weapon_slot::primary
      : navbot_weapon_slot::secondary;
  }

  auto* enemy = choose_navbot_enemy(localplayer);
  auto desired_slot = goal_is_combat(goal)
    ? choose_combat_slot(localplayer, goal, enemy)
    : choose_default_slot(localplayer->get_tf_class());

  if (weapon_slot_loaded(localplayer, desired_slot))
  {
    return desired_slot;
  }

  constexpr navbot_weapon_slot fallback_slots[] = {
    navbot_weapon_slot::primary,
    navbot_weapon_slot::secondary,
    navbot_weapon_slot::melee
  };
  for (auto slot : fallback_slots)
  {
    if (slot != desired_slot && weapon_slot_loaded(localplayer, slot))
    {
      return slot;
    }
  }
  for (auto slot : fallback_slots)
  {
    if (weapon_slot_available(localplayer, slot))
    {
      return slot;
    }
  }

  return navbot_weapon_slot::none;
}

const char* weapon_slot_command(navbot_weapon_slot slot)
{
  switch (slot)
  {
    case navbot_weapon_slot::primary:
      return "slot1";
    case navbot_weapon_slot::secondary:
      return "slot2";
    case navbot_weapon_slot::melee:
      return "slot3";
    case navbot_weapon_slot::none:
    default:
      return nullptr;
  }
}

std::string sanitize_level_name(const char* raw_name)
{
  if (raw_name == nullptr)
  {
    return {};
  }

  auto map_name = std::string(raw_name);
  auto slash = map_name.find_last_of("/\\");
  if (slash != std::string::npos)
  {
    map_name = map_name.substr(slash + 1);
  }

  if (map_name.ends_with(".bsp"))
  {
    map_name.resize(map_name.size() - 4);
  }

  return map_name;
}

bool same_goal_destination(const navbot_goal_state& left, const navbot_goal_state& right)
{
  if (left.valid != right.valid)
  {
    return false;
  }
  if (!left.valid)
  {
    return true;
  }

  if (left.goal.type != right.goal.type || left.goal.destination_area.value != right.goal.destination_area.value)
  {
    return false;
  }
  if (left.goal.entity_index != right.goal.entity_index)
  {
    return false;
  }

  auto shift_limit = goal_is_supply(left.goal.type) ? 24.0f : 96.0f;
  if (goal_is_payload(left.goal.type))
  {
    shift_limit = 40.0f;
  }
  if (left.goal.type == goal_type::heal_follow)
  {
    shift_limit = 72.0f;
  }

  return goal_destination_shift_sq(left, right) <= shift_limit * shift_limit;
}

bool should_replace_goal(const navbot_goal_state& active_goal, const navbot_goal_state& next_goal, bool has_path, Player* localplayer)
{
  if (!next_goal.valid)
  {
    return false;
  }
  if (!active_goal.valid)
  {
    return true;
  }

  if (active_goal.goal.type == goal_type::heal_follow)
  {
    auto* heal_target = medic_automation::controller().heal_target();
    if (heal_target == nullptr || heal_target->is_dormant() || !heal_target->is_alive())
    {
      return true;
    }
    if (next_goal.goal.type != goal_type::heal_follow || next_goal.goal.entity_index != active_goal.goal.entity_index)
    {
      return true;
    }
  }

  if (has_path && goal_is_supply(active_goal.goal.type))
  {
    if (!supply_goal_still_needed(active_goal.goal.type, localplayer))
    {
      return true;
    }

    if (next_goal.goal.type == goal_type::roam || goal_is_supply(next_goal.goal.type))
    {
      return false;
    }
  }

  if (has_path && goal_is_reload(active_goal.goal.type))
  {
    if (!reload_job_still_needed(localplayer))
    {
      return true;
    }

    return next_goal.goal.type == goal_type::reload_weapons
      && !same_goal_destination(active_goal, next_goal);
  }

  if (same_goal_destination(active_goal, next_goal))
  {
    return false;
  }
  if (!has_path)
  {
    return true;
  }
  if (goal_is_payload(active_goal.goal.type) && active_goal.goal.type == next_goal.goal.type)
  {
    return true;
  }
  if (active_goal.goal.type == goal_type::hold_range_on_enemy && next_goal.goal.type == goal_type::hold_range_on_enemy)
  {
    return true;
  }
  if (next_goal.goal.type == active_goal.goal.type)
  {
    return false;
  }
  if (active_goal.goal.type == goal_type::roam)
  {
    return true;
  }

  return next_goal.score > active_goal.score + 20.0f;
}

bool same_nav_edge(nav_edge_id left, nav_edge_id right)
{
  return left.from_area == right.from_area && left.connection_index == right.connection_index;
}

bool nav_edge_valid(nav_edge_id edge_id)
{
  return edge_id.from_area != 0;
}

void reset_debug_runtime(navbot_debug_state& debug_state)
{
  debug_state.goal_valid = false;
  debug_state.has_active_path = false;
  debug_state.active_crumb_count = 0;
  debug_state.current_goal = goal_type::roam;
  debug_state.current_path_status = path_status::failed;
  debug_state.last_failure = follower_failure_reason::none;
}

struct path_spin_runtime_state
{
  const std::vector<crumb>* crumbs = nullptr;
  size_t crumb_index = std::numeric_limits<size_t>::max();
  float remaining_degrees = 0.0f;
  float direction = 1.0f;
  bool active = false;
};

path_spin_runtime_state g_path_spin_state{};

uint32_t path_spin_hash_value(uint32_t value)
{
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

uint32_t path_spin_hash_crumb(const crumb& path_crumb, size_t crumb_index, int command_number)
{
  uint32_t value = static_cast<uint32_t>(crumb_index + 1);
  value ^= path_crumb.area_id.value + 0x9e3779b9u + (value << 6) + (value >> 2);
  value ^= static_cast<uint32_t>(static_cast<int>(std::floor(path_crumb.world.x))) + 0x9e3779b9u + (value << 6) + (value >> 2);
  value ^= static_cast<uint32_t>(static_cast<int>(std::floor(path_crumb.world.y))) + 0x9e3779b9u + (value << 6) + (value >> 2);
  value ^= static_cast<uint32_t>(command_number) + 0x9e3779b9u + (value << 6) + (value >> 2);
  return path_spin_hash_value(value);
}

void reset_path_spin_state()
{
  g_path_spin_state = {};
  g_path_spin_state.crumb_index = std::numeric_limits<size_t>::max();
}

bool path_spin_trigger_matches(user_cmd* user_cmd, const std::vector<crumb>& crumbs, size_t current_index)
{
  if (current_index >= crumbs.size() || crumbs[current_index].kind == crumb_kind::destination)
  {
    return false;
  }

  switch (config.misc.automation.navbot_look_at_path_spin_trigger_mode)
  {
    case Misc::Automation::navbot_look_at_path_spin_trigger::transition:
      return crumbs[current_index].kind == crumb_kind::transition_center;
    case Misc::Automation::navbot_look_at_path_spin_trigger::interval:
    {
      const int interval = std::clamp(config.misc.automation.navbot_look_at_path_spin_interval, 2, 16);
      return current_index > 0 && current_index % static_cast<size_t>(interval) == 0;
    }
    case Misc::Automation::navbot_look_at_path_spin_trigger::random:
    {
      const int chance = std::clamp(config.misc.automation.navbot_look_at_path_spin_chance, 0, 100);
      if (chance <= 0)
      {
        return false;
      }
      if (chance >= 100)
      {
        return true;
      }
      const int command_number = user_cmd != nullptr ? user_cmd->command_number : 0;
      return static_cast<int>(path_spin_hash_crumb(crumbs[current_index], current_index, command_number) % 100u) < chance;
    }
  }

  return false;
}

bool path_spin_active(user_cmd* user_cmd, const std::vector<crumb>& crumbs, size_t current_index)
{
  if (!config.misc.automation.navbot_look_at_path_spin)
  {
    reset_path_spin_state();
    return false;
  }

  if (g_path_spin_state.crumbs != &crumbs || g_path_spin_state.crumb_index != current_index)
  {
    g_path_spin_state.crumbs = &crumbs;
    g_path_spin_state.crumb_index = current_index;
    g_path_spin_state.active = path_spin_trigger_matches(user_cmd, crumbs, current_index);
    g_path_spin_state.remaining_degrees = g_path_spin_state.active ? 360.0f : 0.0f;
    const int command_number = user_cmd != nullptr ? user_cmd->command_number : 0;
    g_path_spin_state.direction = (path_spin_hash_crumb(crumbs[current_index], current_index, command_number) & 1u) != 0u ? 1.0f : -1.0f;
  }

  return g_path_spin_state.active && g_path_spin_state.remaining_degrees > 0.0f;
}

float path_spin_yaw_move(float tick_interval)
{
  const float spin_speed = std::clamp(config.misc.automation.navbot_look_at_path_spin_speed, 180.0f, 2160.0f);
  const float spin_step = std::min(g_path_spin_state.remaining_degrees, spin_speed * tick_interval);
  g_path_spin_state.remaining_degrees -= spin_step;
  if (g_path_spin_state.remaining_degrees <= 0.001f)
  {
    g_path_spin_state.active = false;
    g_path_spin_state.remaining_degrees = 0.0f;
  }
  return spin_step * g_path_spin_state.direction;
}

void apply_reload_controls(user_cmd* user_cmd)
{
  if (user_cmd == nullptr)
  {
    return;
  }

  user_cmd->buttons &= ~(IN_ATTACK | IN_ATTACK2 | IN_ATTACK3);
  user_cmd->buttons |= IN_RELOAD;
}

float normalize_angle_180(float angle)
{
  while (angle > 180.0f)
  {
    angle -= 360.0f;
  }
  while (angle < -180.0f)
  {
    angle += 360.0f;
  }
  return angle;
}

size_t path_look_start_index(const std::vector<crumb>& crumbs, size_t current_index)
{
  if (crumbs.empty())
  {
    return 0;
  }

  const int crumb_offset = std::clamp(config.misc.automation.navbot_look_at_path_crumb_offset, 0, 8);
  return std::min(current_index + static_cast<size_t>(crumb_offset), crumbs.size() - 1);
}

Vec3 compute_smooth_path_look_target(const std::vector<crumb>& crumbs, size_t current_index, const Vec3& eye_origin, float velocity_2d)
{
  if (crumbs.empty() || current_index >= crumbs.size())
  {
    return eye_origin;
  }

  const float look_ahead_min = std::clamp(config.misc.automation.navbot_look_at_path_ahead_min, 0.0f, 900.0f);
  const float look_ahead_max = std::clamp(config.misc.automation.navbot_look_at_path_ahead_max, look_ahead_min, 1200.0f);
  const float look_ahead_base = std::clamp(config.misc.automation.navbot_look_at_path_ahead_base, 0.0f, 900.0f);
  const float look_ahead_velocity_scale = std::clamp(config.misc.automation.navbot_look_at_path_ahead_velocity_scale, 0.0f, 1.5f);
  const float look_ahead = std::clamp(velocity_2d * look_ahead_velocity_scale + look_ahead_base, look_ahead_min, look_ahead_max);
  float remaining = look_ahead;

  Vec3 last_point = eye_origin;
  Vec3 result = crumbs[current_index].world;
  for (size_t i = current_index; i < crumbs.size(); ++i)
  {
    const Vec3& point = crumbs[i].world;
    float dx = point.x - last_point.x;
    float dy = point.y - last_point.y;
    float seg_dist = std::sqrt(dx * dx + dy * dy);

    if (seg_dist >= remaining)
    {
      float t = remaining / std::max(seg_dist, 0.001f);
      return Vec3{
        last_point.x + dx * t,
        last_point.y + dy * t,
        last_point.z + (point.z - last_point.z) * t
      };
    }

    remaining -= seg_dist;
    last_point = point;
    result = point;
  }
  return result;
}

Vec3 compute_og_path_look_target(const std::vector<crumb>& crumbs, size_t current_index, const Vec3& eye_origin)
{
  if (crumbs.empty() || current_index >= crumbs.size())
  {
    return eye_origin;
  }

  return crumbs[current_index].world;
}

Vec3 compute_path_look_target(const std::vector<crumb>& crumbs, size_t current_index, const Vec3& eye_origin, float velocity_2d)
{
  const size_t start_index = path_look_start_index(crumbs, current_index);
  if (config.misc.automation.navbot_look_mode == Misc::Automation::navbot_look_at_path_mode::og)
  {
    return compute_og_path_look_target(crumbs, start_index, eye_origin);
  }

  return compute_smooth_path_look_target(crumbs, start_index, eye_origin, velocity_2d);
}

void apply_look_at_path(Player* localplayer, user_cmd* user_cmd, const std::vector<crumb>& crumbs, size_t current_index)
{
  if (localplayer == nullptr || user_cmd == nullptr || global_vars == nullptr || crumbs.empty() || current_index >= crumbs.size())
  {
    return;
  }

  Vec3 velocity = localplayer->get_velocity();
  float velocity_2d = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);

  Vec3 eye_origin = localplayer->get_origin() + localplayer->get_view_offset();
  Vec3 target = compute_path_look_target(crumbs, current_index, eye_origin, velocity_2d);

  Vec3 delta = Vec3{target.x - eye_origin.x, target.y - eye_origin.y, target.z - eye_origin.z};
  float planar_distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
  if (planar_distance <= 0.001f)
  {
    return;
  }

  float height_delta = std::clamp(delta.z, -72.0f, 96.0f);
  float pitch_factor = height_delta >= 0.0f
    ? std::clamp(config.misc.automation.navbot_look_at_path_pitch_up_scale, 0.0f, 1.0f)
    : std::clamp(config.misc.automation.navbot_look_at_path_pitch_down_scale, 0.0f, 1.0f);
  float focus_z = eye_origin.z + height_delta * pitch_factor;
  float pitch_delta = focus_z - eye_origin.z;
  float pitch_limit = std::clamp(config.misc.automation.navbot_look_at_path_pitch_limit, 0.0f, 89.0f);

  const bool og_mode = config.misc.automation.navbot_look_mode == Misc::Automation::navbot_look_at_path_mode::og;
  float desired_pitch = og_mode
    ? 0.0f
    : std::clamp(-std::atan2(pitch_delta, planar_distance) * radpi, -pitch_limit, pitch_limit);
  float desired_yaw = std::atan2(delta.y, delta.x) * radpi;

  float pitch_speed = std::clamp(config.misc.automation.navbot_look_at_path_pitch_speed, 15.0f, 720.0f);
  float yaw_speed = std::clamp(config.misc.automation.navbot_look_at_path_speed, 45.0f, 1080.0f);
  const float tick_interval = global_vars->interval_per_tick > 0.0f ? global_vars->interval_per_tick : TICK_INTERVAL;
  float pitch_step = pitch_speed * tick_interval;
  float yaw_step = yaw_speed * tick_interval;

  float current_pitch = user_cmd->view_angles.x;
  float current_yaw = user_cmd->view_angles.y;
  float d_pitch = normalize_angle_180(desired_pitch - current_pitch);
  float d_yaw = normalize_angle_180(desired_yaw - current_yaw);

  float pitch_scale = std::min(1.0f, std::fabs(d_pitch) / 30.0f + 0.2f);
  float yaw_scale = std::min(1.0f, std::fabs(d_yaw) / 45.0f + 0.25f);

  float pitch_move = std::clamp(d_pitch, -pitch_step * pitch_scale, pitch_step * pitch_scale);
  float yaw_move = path_spin_active(user_cmd, crumbs, current_index)
    ? path_spin_yaw_move(tick_interval)
    : std::clamp(d_yaw, -yaw_step * yaw_scale, yaw_step * yaw_scale);

  user_cmd->view_angles.x = std::clamp(normalize_angle_180(current_pitch + pitch_move), -89.0f, 89.0f);
  user_cmd->view_angles.y = normalize_angle_180(current_yaw + yaw_move);
  user_cmd->view_angles.z = 0.0f;

  if (prediction != nullptr)
  {
    Vec3 predicted_angles = user_cmd->view_angles;
    prediction->set_local_view_angles(predicted_angles);
    prediction->set_view_angles(predicted_angles);
  }

  if (engine != nullptr)
  {
    Vec3 engine_angles = user_cmd->view_angles;
    engine->set_view_angles(engine_angles);
  }
}

} // namespace

void navbot_controller::clear_runtime_state()
{
  follower_.clear();
  active_path_ = path_result{};
  active_goal_ = {};
  pending_job_ = {};
  next_goal_retry_time_ = 0.0f;
  next_hazard_update_time_ = 0.0f;
  next_weapon_switch_time_ = 0.0f;
  last_requested_weapon_slot_ = 0;
  pending_desired_weapon_slot_ = 0;
  pending_desired_since_ = 0.0f;
  crumb_failure_ = {};
  suppress_aimbot_for_reload_ = false;
  reset_debug_runtime(debug_state_);
}

bool navbot_controller::record_crumb_failure(const follower_tick_result& follow_result, float current_time)
{
  if (!follow_result.failed)
  {
    return false;
  }

  if (!follow_result.failed_crumb_area.valid() && !nav_edge_valid(follow_result.failed_edge))
  {
    crumb_failure_ = {};
    return false;
  }

  auto blacklist_seconds = std::clamp(config.misc.automation.navbot_crumb_blacklist_seconds, 50.0f, 150.0f);
  hazards_.refresh_crumb_blacklists(current_time, blacklist_seconds);

  auto same_failed_crumb = crumb_failure_.area_id.value == follow_result.failed_crumb_area.value
    && same_nav_edge(crumb_failure_.edge_id, follow_result.failed_edge);
  if (!same_failed_crumb || current_time - crumb_failure_.last_failure_time > blacklist_seconds)
  {
    crumb_failure_.area_id = follow_result.failed_crumb_area;
    crumb_failure_.edge_id = follow_result.failed_edge;
    crumb_failure_.count = 1;
    crumb_failure_.last_failure_time = current_time;
    return false;
  }

  ++crumb_failure_.count;
  crumb_failure_.last_failure_time = current_time;
  if (crumb_failure_.count < 2)
  {
    return false;
  }

  hazards_.add_crumb_blacklist(follow_result.failed_crumb_area, follow_result.failed_edge, current_time, blacklist_seconds);
  crumb_failure_ = {};
  return true;
}

int navbot_controller::current_captured_point_index() const
{
  if (last_captured_point_index_ >= 0)
  {
    return last_captured_point_index_;
  }

  int highest_point_index = -1;
  for (auto* entity : entity_cache[class_id::OBJECTIVE_RESOURCE])
  {
    TeamObjectiveResource* objective = reinterpret_cast<TeamObjectiveResource*>(entity);
    if (objective == nullptr)
    {
      continue;
    }

    const int point_count = std::clamp(objective->get_num_control_points(), 0, MAX_CONTROL_POINTS);
    const bool playing_mini_rounds = objective->is_playing_mini_rounds();
    for (int point_index = 0; point_index < point_count; ++point_index)
    {
      if (playing_mini_rounds && !objective->is_in_mini_round(point_index))
      {
        continue;
      }

      const int owning_team = objective->get_owning_team(point_index);
      if (owning_team == team_unassigned)
      {
        continue;
      }

      if (owning_team == tf_team_blue_value)
      {
        highest_point_index = std::max(highest_point_index, point_index);
      }
    }
  }

  return highest_point_index;
}

uint32_t navbot_controller::current_mini_round_mask() const
{
  uint32_t mask = 0;
  for (auto* entity : entity_cache[class_id::OBJECTIVE_RESOURCE])
  {
    TeamObjectiveResource* objective = reinterpret_cast<TeamObjectiveResource*>(entity);
    if (objective == nullptr)
    {
      continue;
    }

    const int point_count = std::clamp(objective->get_num_control_points(), 0, MAX_CONTROL_POINTS);
    for (int point_index = 0; point_index < point_count; ++point_index)
    {
      if (objective->is_in_mini_round(point_index))
      {
        mask |= 1u << static_cast<uint32_t>(point_index);
      }
    }
  }

  return mask;
}

bool navbot_controller::should_block_pathing(Player* localplayer) const
{
  if (localplayer == nullptr)
  {
    return false;
  }

  if (localplayer->in_cond(TF_COND_TAUNTING))
  {
    return true;
  }

  bool waiting_for_players = false;
  int round_state = -1;
  bool in_setup = false;
  bool has_round_state = false;
  bool has_setup_state = false;
  bool has_waiting_state = false;

  if (entity_list != nullptr)
  {
    Entity* proxy = entity_list->get_game_rules_proxy();
    if (proxy != nullptr)
    {
      static const int waiting_offset = tf2_netvars::find_offset("DT_TFGameRulesProxy", { "m_bInWaitingForPlayers" });
      static const int state_offset = tf2_netvars::find_offset("DT_TFGameRulesProxy", { "m_iRoundState" });
      static const int setup_offset = tf2_netvars::find_offset("DT_TFGameRulesProxy", { "m_bInSetup" });

      if (waiting_offset != 0)
      {
        waiting_for_players = *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(proxy) + waiting_offset);
        has_waiting_state = true;
      }
      if (state_offset != 0)
      {
        const int read_round_state = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(proxy) + state_offset);
        if (round_state_is_valid(read_round_state))
        {
          round_state = read_round_state;
          has_round_state = true;
        }
      }
      if (setup_offset != 0)
      {
        in_setup = *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(proxy) + setup_offset);
        has_setup_state = true;
      }
    }
  }

  if (has_round_state && round_state >= gr_state_team_win)
  {
    return true;
  }

  const std::string map_name = mesh_.map_name().empty() ? loaded_map_name_ : mesh_.map_name();
  const bool on_cp_or_pl_map = map_has_cp_or_pl_prefix(map_name);
  const bool is_pipeline = map_name.starts_with("plr_");

  bool warmup_active = false;
  bool setup_active = false;
  bool match_fully_started = false;

  if (has_round_state || has_waiting_state)
  {
    warmup_active = waiting_for_players || (has_round_state && round_state_is_warmup(round_state));
    setup_active = has_setup_state && in_setup && on_cp_or_pl_map && !is_pipeline;
    match_fully_started = !waiting_for_players && has_round_state && round_state_has_started(round_state);
  }
  else
  {
    warmup_active = warmup_active_ || !round_started_;
    setup_active = round_started_ && on_cp_or_pl_map && !is_pipeline && !setup_finished_;
    match_fully_started = round_started_ && (!on_cp_or_pl_map || is_pipeline || setup_finished_);
  }

  auto local_team = localplayer->get_team();

  if (local_team == tf_team::BLU && setup_active)
  {
    return true;
  }

  const auto block_during = config.misc.automation.navbot_block_during_enum;

  if (warmup_active)
  {
    if (block_during == Misc::Automation::navbot_block_during::warmup ||
        block_during == Misc::Automation::navbot_block_during::warmup_and_setup)
    {
      if (!config.misc.automation.navbot_warmup_only_blu_cp_pl)
      {
        return true;
      }
      return local_team == tf_team::BLU && on_cp_or_pl_map;
    }
    return false;
  }

  if (setup_active && block_during == Misc::Automation::navbot_block_during::warmup_and_setup)
  {
    return true;
  }

  return false;
}

void navbot_controller::on_create_move(user_cmd* user_cmd)
{
  suppress_aimbot_for_reload_ = false;
  if (!config.misc.automation.navbot_enabled)
  {
    return;
  }

  ensure_started();

  if (engine == nullptr || !engine->is_in_game())
  {
    clear_runtime_state();
    round_started_ = false;
    setup_finished_ = false;
    warmup_active_ = false;
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive() || localplayer->get_team() == tf_team::UNKNOWN)
  {
    clear_runtime_state();
    return;
  }

  rebuild_mesh_if_needed();
  if (!round_started_ && !warmup_active_)
  {
    round_started_ = true;
    if (map_has_cp_or_pl_prefix(loaded_map_name_))
    {
      setup_finished_ = true;
    }
  }
  if (should_block_pathing(localplayer))
  {
    clear_runtime_state();
    return;
  }

  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  hazards_.update_expired(current_time);
  poll_path_results();

  if (active_goal_.valid && active_goal_.goal.type == goal_type::reload_weapons && !reload_job_still_needed(localplayer))
  {
    jobs_.cancel_generation(current_generation_id_);
    ++current_generation_id_;
    pending_job_ = {};
    next_goal_refresh_time_ = 0.0f;
    next_path_request_time_ = 0.0f;
    follower_.clear();
    active_path_ = path_result{};
    active_goal_ = {};
  }

  if (active_goal_.valid && active_goal_.goal.type == goal_type::heal_follow)
  {
    auto* heal_target = medic_automation::controller().heal_target();
    if (heal_target == nullptr
      || heal_target->is_dormant()
      || !heal_target->is_alive()
      || heal_target->get_team() != localplayer->get_team()
      || heal_target->get_index() != active_goal_.goal.entity_index)
    {
      jobs_.cancel_generation(current_generation_id_);
      ++current_generation_id_;
      pending_job_ = {};
      next_goal_refresh_time_ = 0.0f;
      next_path_request_time_ = 0.0f;
      follower_.clear();
      active_path_ = path_result{};
      active_goal_ = {};
    }
  }

  const auto has_active_path_or_pending_request = follower_.has_path() || pending_job_.generation_id == current_generation_id_;
  const bool needs_new_goal = !active_goal_.valid || !has_active_path_or_pending_request;
  if ((needs_new_goal && current_time >= next_goal_retry_time_) || current_time >= next_goal_refresh_time_)
  {
    next_goal_refresh_time_ = current_time + goal_refresh_interval;
    next_goal_retry_time_ = current_time + goal_retry_interval;

    auto next_goal = goals_.select_goal(mesh_, localplayer, current_time);
    if (should_replace_goal(active_goal_, next_goal, has_active_path_or_pending_request, localplayer))
    {
      jobs_.cancel_generation(current_generation_id_);
      active_goal_ = next_goal;
      crumb_failure_ = {};
      ++current_generation_id_;
      pending_job_ = {};
      next_path_request_time_ = 0.0f;
    }
  }

  debug_state_.current_goal = active_goal_.goal.type;
  debug_state_.active_generation_id = current_generation_id_;
  debug_state_.active_world_generation = world_generation_id_;
  debug_state_.mesh_ready = mesh_.is_ready();
  debug_state_.goal_valid = active_goal_.valid;
  debug_state_.map_name = mesh_.map_name();
  debug_state_.nav_file_path = mesh_.nav_file_path();
  debug_state_.captured_point_index = current_captured_point_index();
  debug_state_.mini_round_mask = current_mini_round_mask();
  debug_state_.setup_finished = setup_finished_;
  server_recording_context recording_context{};
  recording_context.captured_point_index = debug_state_.captured_point_index;
  recording_context.mini_round_mask = debug_state_.mini_round_mask;
  recording_context.setup_finished = setup_finished_;
  server_recorder_.update(mesh_.map_name(), recording_context, current_time);
  const server_recording_status& recording_status = server_recorder_.status();
  debug_state_.server_recording = recording_status.recording;
  debug_state_.server_module_found = recording_status.server_module_found;
  debug_state_.server_signature_found = recording_status.signature_found;
  debug_state_.server_recording_write_ok = recording_status.write_ok;
  debug_state_.server_recorded_total_areas = recording_status.server_area_count;
  debug_state_.server_recorded_blocked_areas = recording_status.blocked_area_count;
  debug_state_.server_recorded_unique_areas = recording_status.unique_blocked_area_count;
  debug_state_.server_recorded_snapshots = recording_status.snapshot_count;
  debug_state_.server_recording_path = recording_status.output_path;
  debug_state_.server_recording_message = recording_status.message;
  request_path_if_needed();
  if (active_goal_.valid && active_goal_.goal.type == goal_type::reload_weapons && reload_job_still_needed(localplayer))
  {
    suppress_aimbot_for_reload_ = true;
    apply_reload_controls(user_cmd);
  }
  else
  {
    update_weapon_choice(localplayer);
  }

  auto follow_result = follower_.tick(mesh_, localplayer, user_cmd, current_time);
  debug_state_.has_active_path = follower_.has_path();
  debug_state_.active_crumb_count = static_cast<uint32_t>(follower_.crumbs().size());

  if (config.misc.movement.moonwalk
    && config.misc.movement.moonwalk_navbot_compat
    && !localplayer->is_scoped()
    && follower_.has_path()
    && (user_cmd->buttons & IN_JUMP) == 0
    && !follower_.is_stuck(current_time))
  {
    user_cmd->buttons |= IN_DUCK;
  }

  navbot_update_throwable_look_suppress(localplayer->get_weapon(), user_cmd, current_time);
  if (config.misc.automation.navbot_look_at_path && !navbot_throwable_look_suppresses_path_look(current_time))
  {
    if (follower_.current_crumb() != nullptr)
    {
      apply_look_at_path(localplayer, user_cmd, follower_.crumbs(), follower_.current_crumb_index());
    }
  }

  if (follow_result.failed)
  {
    const auto blacklisted_crumb = record_crumb_failure(follow_result, current_time);
    if (follow_result.failure_reason == follower_failure_reason::hazard_intersection
      && !blacklisted_crumb
      && !hazards_.has_active_world_hazard(current_time))
    {
      debug_state_.last_failure = follower_failure_reason::none;
      return;
    }

    debug_state_.last_failure = follow_result.failure_reason;
    debug_state_.has_active_path = false;
    if ((blacklisted_crumb || follow_result.failure_reason == follower_failure_reason::hazard_intersection) && active_goal_.valid)
    {
      jobs_.cancel_generation(current_generation_id_);
      pending_job_ = {};
      next_path_request_time_ = current_time;
      follower_.clear();
      active_path_ = path_result{};
      return;
    }

    jobs_.cancel_generation(current_generation_id_);
    ++current_generation_id_;
    pending_job_ = {};
    next_goal_refresh_time_ = 0.0f;
    next_path_request_time_ = current_time + path_retry_interval;
    follower_.clear();
    active_path_ = path_result{};
  }
}

void navbot_controller::update_weapon_choice(Player* localplayer)
{
  if (!config.misc.automation.navbot_auto_weapon || localplayer == nullptr || engine == nullptr)
  {
    return;
  }

  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  if (current_time < next_weapon_switch_time_)
  {
    return;
  }

  auto* active_weapon = localplayer->get_weapon();
  if (active_weapon != nullptr)
  {
    if ((active_weapon->is_sniper_rifle() && localplayer->is_scoped())
      || (active_weapon->is_minigun() && localplayer->is_heavy_revved()))
    {
      return;
    }

    auto next_primary = active_weapon->get_next_primary_attack();
    if (next_primary > current_time && (next_primary - current_time) < 0.15f)
    {
      return;
    }
  }

  auto desired_slot = choose_navbot_weapon_slot(localplayer, active_goal_);
  if (desired_slot == navbot_weapon_slot::none)
  {
    return;
  }

  auto current_slot = weapon_slot_for(active_weapon, localplayer->get_tf_class());
  auto desired_slot_value = static_cast<int>(desired_slot);
  if (current_slot == desired_slot)
  {
    last_requested_weapon_slot_ = desired_slot_value;
    pending_desired_weapon_slot_ = desired_slot_value;
    pending_desired_since_ = current_time;
    return;
  }

  constexpr float weapon_switch_stick_time = 0.4f;
  if (pending_desired_weapon_slot_ != desired_slot_value)
  {
    pending_desired_weapon_slot_ = desired_slot_value;
    pending_desired_since_ = current_time;
    return;
  }
  if ((current_time - pending_desired_since_) < weapon_switch_stick_time)
  {
    return;
  }

  auto* slot_command = weapon_slot_command(desired_slot);
  if (slot_command == nullptr)
  {
    return;
  }

  engine->client_cmd_unrestricted(slot_command);
  last_requested_weapon_slot_ = desired_slot_value;
  next_weapon_switch_time_ = current_time + weapon_switch_interval;
}

void navbot_controller::on_frame_stage_notify()
{
  if (!config.misc.automation.navbot_enabled || !engine->is_in_game())
  {
    return;
  }

  rebuild_mesh_if_needed();
  if (global_vars == nullptr || global_vars->curtime >= next_hazard_update_time_)
  {
    update_hazards();
    next_hazard_update_time_ = (global_vars != nullptr ? global_vars->curtime : 0.0f) + hazard_refresh_interval;
  }
}

void navbot_controller::on_game_event(GameEvent* event)
{
  if (!config.misc.automation.navbot_enabled || event == nullptr)
  {
    return;
  }

  auto name = event->get_name();
  if (name == nullptr)
  {
    return;
  }

  if (std::strcmp(name, "teamplay_point_captured") == 0)
  {
    last_captured_point_index_ = event->get_int("cp", -1);
  }

  if (std::strcmp(name, "item_pickup") == 0
    || std::strcmp(name, "teamplay_point_captured") == 0
    || std::strcmp(name, "teamplay_point_unlocked") == 0
    || std::strcmp(name, "teamplay_flag_event") == 0
    || std::strcmp(name, "teamplay_round_start") == 0
    || std::strcmp(name, "teamplay_setup_finished") == 0)
  {
    if (std::strcmp(name, "teamplay_round_start") == 0)
    {
      round_started_ = true;
      setup_finished_ = false;
      warmup_active_ = false;
      last_captured_point_index_ = -1;
    }
    else if (std::strcmp(name, "teamplay_setup_finished") == 0)
    {
      round_started_ = true;
      setup_finished_ = true;
      warmup_active_ = false;
      last_captured_point_index_ = -1;
    }

    jobs_.cancel_generation(current_generation_id_);
    ++world_generation_id_;
    pending_job_ = {};
  }

  if (std::strcmp(name, "teamplay_waiting_begins") == 0
    || std::strcmp(name, "teamplay_restart_round") == 0
    || std::strcmp(name, "teamplay_round_win") == 0)
  {
    round_started_ = false;
    setup_finished_ = false;
    warmup_active_ = true;
    last_captured_point_index_ = -1;
    jobs_.cancel_generation(current_generation_id_);
    ++world_generation_id_;
    pending_job_ = {};
  }
}

void navbot_controller::draw_imgui()
{
  if (!config.misc.automation.navbot_enabled)
  {
    return;
  }

  auto* draw_list = ImGui::GetBackgroundDrawList();
  if (draw_list == nullptr)
  {
    return;
  }

  if (config.misc.automation.navbot_draw_path && active_path_.status == path_status::success)
  {
    auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
    draw_path_exact_imgui(draw_list, active_path_, follower_.current_crumb_index(), current_time, follower_.reached_crumb_times());
  }

  if (config.misc.automation.navbot_debug_text)
  {
    draw_debug_overlay_imgui(draw_list, debug_state_);
  }
}

const navbot_debug_state& navbot_controller::debug_state() const
{
  return debug_state_;
}

bool navbot_controller::should_suppress_aimbot() const
{
  return config.misc.automation.navbot_enabled && suppress_aimbot_for_reload_;
}

bool navbot_controller::should_prioritize_danger_movement() const
{
  if (!config.misc.automation.navbot_enabled)
  {
    return false;
  }

  return active_goal_.valid && active_goal_.goal.type == goal_type::escape_danger;
}

void navbot_controller::ensure_started()
{
  if (jobs_started_)
  {
    return;
  }

  jobs_.start(&mesh_, &hazards_);
  jobs_started_ = true;
}

void navbot_controller::rebuild_mesh_if_needed()
{
  auto current_map_name = engine != nullptr ? sanitize_level_name(engine->get_level_name()) : std::string{};
  if (current_map_name == loaded_map_name_)
  {
    return;
  }

  loaded_map_name_ = current_map_name;
  jobs_.cancel_generation(current_generation_id_);
  mesh_.rebuild_from_current_map();
  hazards_.clear();
  server_recorder_.reset();
  clear_runtime_state();
  ++world_generation_id_;
  ++current_generation_id_;
  next_goal_refresh_time_ = 0.0f;
  next_path_request_time_ = 0.0f;
  round_started_ = false;
  setup_finished_ = false;
  warmup_active_ = false;
  last_captured_point_index_ = -1;
  debug_state_.mesh_ready = mesh_.is_ready();
  debug_state_.map_name = mesh_.map_name();
  debug_state_.nav_file_path = mesh_.nav_file_path();
}

void navbot_controller::poll_path_results()
{
  while (true)
  {
    auto result = jobs_.poll_path_result();
    if (!result.has_value())
    {
      break;
    }

    auto& path = result->result;
    if (path.generation_id != current_generation_id_
      || path.world_generation != world_generation_id_
      || path.hazard_generation != hazards_.generation())
    {
      ++debug_state_.stale_result_count;
      continue;
    }
    pending_job_ = {};
    if (!active_goal_.valid || path.status != path_status::success)
    {
      debug_state_.current_path_status = path.status;
      if (path.status == path_status::no_path)
      {
        ++debug_state_.rejected_job_count;
        next_goal_refresh_time_ = 0.0f;
        next_path_request_time_ = (global_vars != nullptr ? global_vars->curtime : 0.0f) + path_retry_interval;
        if (active_goal_.valid && active_goal_.goal.type == goal_type::roam)
        {
          active_goal_ = {};
        }
      }
      continue;
    }

    active_path_ = path;
    follower_.set_path(path_result(path));
    debug_state_.current_path_status = path.status;
    debug_state_.last_solve_time_ms = path.solve_time_ms;
    debug_state_.has_active_path = true;
    debug_state_.active_crumb_count = static_cast<uint32_t>(path.crumbs.size());
  }
}

void navbot_controller::request_path_if_needed()
{
  debug_state_.path_request_message = "checking";
  if (!active_goal_.valid || !mesh_.is_ready())
  {
    debug_state_.path_request_message = !active_goal_.valid ? "no goal" : "mesh missing";
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr)
  {
    debug_state_.path_request_message = "no localplayer";
    return;
  }
  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;

  if (pending_job_.generation_id == current_generation_id_ || follower_.generation_id() == current_generation_id_)
  {
    debug_state_.path_request_message = pending_job_.generation_id == current_generation_id_ ? "pending" : "active";
    return;
  }
  if (current_time < next_path_request_time_)
  {
    debug_state_.path_request_message = "waiting retry";
    return;
  }

  auto start_area = mesh_.find_closest_area(localplayer->get_origin());
  auto goal_area = active_goal_.goal.destination_area;
  if (!start_area.valid() || !goal_area.valid())
  {
    debug_state_.path_request_message = !start_area.valid() ? "no start area" : "no goal area";
    return;
  }

  path_request request{};
  request.request_id = next_request_id_++;
  request.generation_id = current_generation_id_;
  request.world_generation = world_generation_id_;
  request.goal = active_goal_.goal.type;
  request.start_area = start_area;
  request.goal_area = goal_area;
  request.start_world = localplayer->get_origin();
  request.goal_world = active_goal_.goal.destination;
  request.team = static_cast<uint32_t>(localplayer->get_team());
  request.class_id = static_cast<uint32_t>(localplayer->get_tf_class());
  request.hazard_generation = hazards_.generation();
  request.captured_point_index = current_captured_point_index();
  request.recorded_blocked_areas = server_recorder_.blocked_areas();
  request.clearance = path_clearance_for_player(localplayer);
  request.destination_reach_distance = destination_reach_distance_for_goal(active_goal_.goal.type);
  request.setup_finished = setup_finished_;
  request.require_exact_goal_area = active_goal_.goal.type == goal_type::push_payload;

  pending_job_ = jobs_.submit_path_request(request);
  debug_state_.path_request_message = "submitted";
  next_path_request_time_ = current_time;
}

namespace
{

int hazard_priority(hazard_kind kind)
{
  switch (kind)
  {
    case hazard_kind::sentry: return 100;
    case hazard_kind::sticky: return 80;
    case hazard_kind::enemy_pressure: return 30;
    case hazard_kind::static_blocked:
    case hazard_kind::transition_failure:
    case hazard_kind::crumb_blacklist:
    default:
      return 0;
  }
}

} // namespace

void navbot_controller::update_hazards()
{
  if constexpr (textmode_build)
  {
    hazards_.clear_soft_costs();
    return;
  }

  auto* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (localplayer == nullptr || !mesh_.is_ready())
  {
    return;
  }

  auto local_team = localplayer->get_team();
  auto local_class = localplayer->get_tf_class();
  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;

  hazards_.clear_soft_costs();

  std::unordered_map<uint32_t, hazard_record> per_area_hazards;
  per_area_hazards.reserve(128);

  auto apply_hazard = [&](nav_area_id area_id, hazard_kind kind, float cost, float expire_time)
  {
    if (!area_id.valid() || cost <= 0.0f)
    {
      return;
    }

    auto incoming_priority = hazard_priority(kind);
    auto& slot = per_area_hazards[area_id.value];
    auto existing_priority = slot.area_id.valid() ? hazard_priority(slot.kind) : -1;

    if (incoming_priority > existing_priority)
    {
      slot.kind = kind;
      slot.policy = hazard_policy::soft_cost;
      slot.area_id = area_id;
      slot.cost = std::max(slot.cost, cost);
      slot.expire_time = std::max(slot.expire_time, expire_time);
    }
    else if (incoming_priority == existing_priority)
    {
      slot.cost = std::max(slot.cost, cost);
      slot.expire_time = std::max(slot.expire_time, expire_time);
    }
  };

  constexpr float hazard_expire_seconds = 0.4f;
  const auto hazard_expire = current_time + hazard_expire_seconds;

  auto enemy_radius_for = [](tf_class cls)
  {
    switch (cls)
    {
      case tf_class::SNIPER:    return 1100.0f;
      case tf_class::HEAVYWEAPONS:
      case tf_class::ENGINEER:
      case tf_class::SCOUT:     return 320.0f;
      case tf_class::PYRO:      return 280.0f;
      case tf_class::SPY:       return 240.0f;
      default:                  return 500.0f;
    }
  };

  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (player == nullptr || player == localplayer || player->get_team() == local_team)
    {
      continue;
    }
    if (player->is_dormant() || !player->is_alive())
    {
      continue;
    }

    auto enemy_origin = player->get_origin();
    auto enemy_area_id = mesh_.find_closest_area(enemy_origin);
    if (enemy_area_id.valid() && mesh_.area_has_flag(enemy_area_id, nav_area_flag_spawn_room))
    {
      continue;
    }

    auto invuln = player->is_invulnerable();
    auto base_cost = invuln ? 1200.0f : 280.0f;
    auto enemy_class = player->get_tf_class();
    if (enemy_class == tf_class::SNIPER)
    {
      base_cost *= 1.8f;
    }

    auto radius = enemy_radius_for(enemy_class);
    auto areas = mesh_.areas_in_radius(enemy_origin, radius);
    auto radius_sq = radius * radius;
    auto kind = invuln ? hazard_kind::sentry : hazard_kind::enemy_pressure;

    for (const auto& nearby : areas)
    {
      auto falloff = 1.0f - std::clamp(nearby.distance_sq / radius_sq, 0.0f, 1.0f);
      auto cost = base_cost * (0.4f + 0.6f * falloff);
      apply_hazard(nearby.id, kind, cost, hazard_expire);
    }
  }

  constexpr float sentry_inner = 800.0f;
  constexpr float sentry_mid = 1050.0f;
  constexpr float sentry_outer = 1200.0f;

  for (auto* entity : entity_cache[class_id::SENTRY])
  {
    if (entity == nullptr || entity->is_dormant() || entity->get_team() == local_team)
    {
      continue;
    }

    auto sentry_origin = entity->get_origin();
    auto areas = mesh_.areas_in_radius(sentry_origin, sentry_outer);
    for (const auto& nearby : areas)
    {
      auto distance = std::sqrt(nearby.distance_sq);
      auto cost = 0.0f;
      if (distance <= sentry_inner)
      {
        auto falloff = 1.0f - std::clamp(distance / sentry_inner, 0.0f, 1.0f);
        cost = 800.0f + 400.0f * falloff;
      }
      else if (distance <= sentry_mid)
      {
        cost = 500.0f;
      }
      else
      {
        cost = 250.0f;
        if (local_class == tf_class::HEAVYWEAPONS || local_class == tf_class::SOLDIER)
        {
          cost *= 0.4f;
        }
      }

      if (const auto* area_data = mesh_.find_area(nearby.id))
      {
        auto vertical_delta = std::fabs(sentry_origin.z - area_data->center.z);
        if (vertical_delta > 200.0f)
        {
          cost *= 0.6f;
        }
      }

      apply_hazard(nearby.id, hazard_kind::sentry, cost, hazard_expire);
    }
  }

  constexpr float sticky_radius = 150.0f;
  for (auto* entity : entity_cache[class_id::PILL_OR_STICKY])
  {
    if (entity == nullptr || entity->is_dormant() || entity->get_team() == local_team)
    {
      continue;
    }

    auto sticky_origin = entity->get_origin();
    auto areas = mesh_.areas_in_radius(sticky_origin, sticky_radius);
    auto radius_sq = sticky_radius * sticky_radius;
    for (const auto& nearby : areas)
    {
      auto falloff = 1.0f - std::clamp(nearby.distance_sq / radius_sq, 0.0f, 1.0f);
      auto cost = 900.0f * (0.5f + 0.5f * falloff);
      apply_hazard(nearby.id, hazard_kind::sticky, cost, current_time + 1.5f);
    }
  }

  for (auto& [_, record] : per_area_hazards)
  {
    hazards_.add_area_hazard(record);
  }
}

navbot_controller& controller()
{
  if (global_controller == nullptr)
  {
    static navbot_controller instance{};
    global_controller = &instance;
  }

  return *global_controller;
}

} // namespace navbot
