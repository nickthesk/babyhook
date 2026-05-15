/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/combat/random_crits/random_crits.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef RANDOM_CRITS_HPP
#define RANDOM_CRITS_HPP

#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"

namespace random_crits
{

struct indicator_state
{
  bool available = false;
  bool enabled = false;
  bool force_mode = false;
  bool save_mode = false;
  bool melee_weapon = false;
  bool crit_boosted = false;
  bool can_attack = false;
  bool selected = false;
  bool forcing = false;
  bool skipping = false;
  bool can_random_crit = false;
  bool rapid_fire = false;
  bool crit_banned = false;
  float bucket = 0.0f;
  float bucket_cap = 1000.0f;
  float bucket_progress = 0.0f;
  float crit_chance = 0.0f;
  float crit_chance_mult = 1.0f;
  float damage = 0.0f;
  float crit_cost = 0.0f;
  float damage_to_crit = 0.0f;
  float rapid_wait = 0.0f;
  float streaming_time = 0.0f;
  int checks = 0;
  int seed_requests = 0;
  int available_crits = 0;
  int potential_crits = 0;
  int next_crit = 0;
  int current_seed = 0;
  int selected_command = 0;
  int selected_seed = 0;
  int selected_roll = -1;
  int selected_offset = 0;
  int seed_scan = 0;
  int next_crit_seed_offset = 0;
  int next_crit_seed_roll = -1;
};

void run(user_cmd* cmd, int host_sequence_number);
bool should_force_attack(Weapon* weapon, user_cmd* cmd);
bool should_skip_attack(Weapon* weapon, user_cmd* cmd);
indicator_state get_indicator_state();

} // namespace random_crits

#endif
