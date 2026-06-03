#include "crit_hack.hpp"

#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/player_manager.hpp"
#include "external/MD5/MD5.hpp"

#include <algorithm>
#include <climits>
#include <cmath>

namespace crit_hack {

namespace {

inline float math_remap(float val, float a, float b, float c, float d) {
  if (a == b) return c;
  return c + (val - a) * (d - c) / (b - a);
}

// Valve LCG Random Stream
class c_valve_random {
public:
  void set_seed(int seed) {
    idum = (seed < 0 ? seed : -seed);
    iy = 0;
  }

  int generate_random_number() {
    int j;
    int k;

    if (idum <= 0 || !iy) {
      if (-idum < 1)
        idum = 1;
      else
        idum = -idum;

      for (j = 32 + 7; j >= 0; j--) {
        k = idum / 127773;
        idum = 16807 * (idum - k * 127773) - 2836 * k;
        if (idum < 0)
          idum += 2147483647;
        if (j < 32)
          iv[j] = idum;
      }
      iy = iv[0];
    }
    k = idum / 127773;
    idum = 16807 * (idum - k * 127773) - 2836 * k;
    if (idum < 0)
      idum += 2147483647;
    j = iy / (1 + (2147483646) / 32);

    if (j >= 32 || j < 0) {
      j &= 32 - 1;
    }

    iy = iv[j];
    iv[j] = idum;

    return iy;
  }

  int random_int(int low, int high) {
    unsigned int max_acceptable;
    unsigned int x = high - low + 1;
    unsigned int n;

    if (x <= 1 || 0x7FFFFFFFUL < x - 1) {
      return low;
    }

    max_acceptable = 0x7FFFFFFFUL - ((0x7FFFFFFFUL + 1) % x);
    do {
      n = generate_random_number();
    } while (n > max_acceptable);

    return low + (n % x);
  }

private:
  int idum = 0;
  int iy = 0;
  int iv[32]{};
};

c_valve_random valve_rand;

int crit_damage = 0;
int ranged_damage = 0;
int melee_damage = 0;
int resource_damage = 0;
int desync_damage = 0;
std::unordered_map<int, health_history_t> health_history{};

bool crit_banned = false;
float damage_till_flip = 0.0f;

float current_damage = 0.0f;
float current_cost = 0.0f;
int available_crits = 0;
int potential_crits = 0;
int next_crit = 0;

int weapon_ent_index = 0;
bool is_melee_weapon = false;
float crit_chance = 0.0f;
float mult_crit_chance = 1.0f;

enum class crit_request {
  any,
  crit,
  skip
};

inline Entity* get_player_resource_entity() {
  if (entity_list == nullptr) {
    return nullptr;
  }

  const int max_entities = entity_list->get_max_entities();
  for (int index = 1; index <= max_entities; ++index) {
    auto* entity = entity_list->entity_from_index(index);
    if (entity != nullptr && entity->get_class_id() == class_id::PLAYER_RESOURCE) {
      return entity;
    }
  }

  return nullptr;
}

template <typename value_type>
inline value_type read_player_resource_value(Entity* player_resource, int array_offset, int player_index) {
  if (player_resource == nullptr || player_index <= 0) {
    return {};
  }

  const auto base = reinterpret_cast<std::uintptr_t>(player_resource);
  const auto entry_offset = static_cast<std::uintptr_t>(array_offset) + (static_cast<std::uintptr_t>(player_index) * sizeof(value_type));
  return *reinterpret_cast<value_type*>(base + entry_offset);
}

int command_to_seed(int command_number, Weapon* weapon, bool melee) {
  int seed = MD5_PseudoRandom(command_number) & std::numeric_limits<int>::max();
  int local_player = engine->get_localplayer_index();
  int mask = melee
    ? (weapon->to_entity()->get_index() << 16) | (local_player << 8)
    : (weapon->to_entity()->get_index() << 8) | local_player;
  return seed ^ mask;
}

bool is_crit_seed(int seed, Weapon* weapon, bool crit, bool safe, bool melee) {
  if (seed == weapon->current_seed())
    return false;

  valve_rand.set_seed(seed);
  int random_val = valve_rand.random_int(0, 9999);

  if (safe) {
    int lower, upper;
    if (melee)
      lower = 1500, upper = 6000;
    else
      lower = 100, upper = 800;
    
    lower = static_cast<int>(lower * mult_crit_chance);
    upper = static_cast<int>(upper * mult_crit_chance);

    if (crit ? lower >= 0 : upper < 10000)
      return crit ? random_val < lower : !(random_val < upper);
  }

  int range = static_cast<int>(crit_chance * 10000.0f);
  return crit ? random_val < range : !(random_val < range);
}

bool is_crit_command(int command_number, Weapon* weapon, bool crit, bool safe, bool melee) {
  int seed = command_to_seed(command_number, weapon, melee);
  return is_crit_seed(seed, weapon, crit, safe, melee);
}

int get_crit_command(Weapon* weapon, int command_number, bool crit, bool safe, bool melee) {
  for (int i = command_number; i < command_number + 4096; i++) {
    if (is_crit_command(i, weapon, crit, safe, melee))
      return i;
  }
  return 0;
}

void update_weapon_info(Player* local, Weapon* weapon) {
  weapon_ent_index = weapon->to_entity()->get_index();
  is_melee_weapon = weapon->is_melee();

  if (is_melee_weapon) {
    crit_chance = 0.15f * local->get_crit_mult();
  } else if (weapon->is_rapid_fire()) {
    crit_chance = 0.02f * local->get_crit_mult();
    float non_crit_duration = (2.0f / crit_chance) - 2.0f;
    crit_chance = 1.0f / non_crit_duration;
  } else {
    crit_chance = 0.02f * local->get_crit_mult();
  }

  mult_crit_chance = attribute_manager->attrib_hook_value(1.0f, "mult_crit_chance", weapon->to_entity());
  crit_chance *= mult_crit_chance;

  static Weapon* old_weapon = nullptr;
  Weapon* old_weapon_prev = old_weapon;
  old_weapon = weapon;

  static float old_bucket = 0.0f;
  const float last_bucket = old_bucket;
  const float bucket = old_bucket = weapon->crit_token_bucket();

  static int old_crit_checks = 0;
  const int last_crit_checks = old_crit_checks;
  const int crit_checks_val = old_crit_checks = weapon->crit_checks();

  static int old_crit_seed_requests = 0;
  const int last_crit_seed_requests = old_crit_seed_requests;
  const int crit_seed_requests_val = old_crit_seed_requests = weapon->crit_seed_requests();

  if (weapon == old_weapon_prev && bucket == last_bucket && crit_checks_val == last_crit_checks && crit_seed_requests_val == last_crit_seed_requests)
    return;

  static Convar* tf_weapon_criticals_bucket_cap = nullptr;
  if (tf_weapon_criticals_bucket_cap == nullptr && convar_system != nullptr) {
    tf_weapon_criticals_bucket_cap = convar_system->find_var("tf_weapon_criticals_bucket_cap");
  }
  const float bucket_cap = tf_weapon_criticals_bucket_cap ? tf_weapon_criticals_bucket_cap->get_float() : 1000.0f;
  bool rapid_fire = weapon->is_rapid_fire();
  float fire_rate = weapon->get_fire_rate();

  float damage = static_cast<float>(weapon->get_damage());
  int projectiles_per_shot = weapon->get_bullets_per_shot();
  if (!is_melee_weapon && projectiles_per_shot > 0)
    projectiles_per_shot = static_cast<int>(attribute_manager->attrib_hook_value(static_cast<float>(projectiles_per_shot), "mult_bullets_per_shot", weapon->to_entity()));
  else
    projectiles_per_shot = 1;

  float base_damage = damage * projectiles_per_shot;
  if (rapid_fire) {
    damage = base_damage * (2.0f / fire_rate);
    if (damage * 3.0f > bucket_cap)
      damage = bucket_cap / 3.0f;
  } else {
    damage = base_damage;
  }

  float mult = is_melee_weapon ? 0.5f : math_remap(static_cast<float>(crit_seed_requests_val + 1) / (crit_checks_val + 1), 0.1f, 1.0f, 1.0f, 3.0f);
  float cost = damage * 3.0f;

  int potential = static_cast<int>((std::max(bucket_cap, bucket) - base_damage) / (3.0f * damage / (is_melee_weapon ? 2.0f : 1.0f) - base_damage));
  int available = 0;
  {
    int test_shots = crit_checks_val;
    int test_crits = crit_seed_requests_val;
    float test_bucket = bucket;
    for (int i = 0; i < 1000; i++) {
      test_shots++;
      test_crits++;

      float test_mult = is_melee_weapon ? 0.5f : math_remap(static_cast<float>(test_crits) / test_shots, 0.1f, 1.0f, 1.0f, 3.0f);
      if (test_bucket < bucket_cap)
        test_bucket = std::min(test_bucket + base_damage, bucket_cap);
      test_bucket -= cost * test_mult;
      if (test_bucket < 0.0f)
        break;

      available++;
    }
  }

  int next = 0;
  if (available != potential) {
    int test_shots = crit_checks_val;
    int test_crits = crit_seed_requests_val;
    float test_bucket = bucket;
    float tick_base = global_vars->curtime;
    float last_rapid_crit_time = weapon->last_rapid_fire_crit_check_time();
    for (int i = 0; i < 1000; i++) {
      int crits = 0;
      {
        int test_shots2 = test_shots;
        int test_crits2 = test_crits;
        float test_bucket2 = test_bucket;
        for (int j = 0; j < 1000; j++) {
          test_shots2++;
          test_crits2++;

          float test_mult = is_melee_weapon ? 0.5f : math_remap(static_cast<float>(test_crits2) / test_shots2, 0.1f, 1.0f, 1.0f, 3.0f);
          if (test_bucket2 < bucket_cap)
            test_bucket2 = std::min(test_bucket2 + base_damage, bucket_cap);
          test_bucket2 -= cost * test_mult;
          if (test_bucket2 < 0.0f)
            break;

          crits++;
        }
      }
      if (available < crits)
        break;

      if (!rapid_fire) {
        test_shots++;
      } else {
        tick_base += std::ceil(fire_rate / 0.015f) * 0.015f;
        if (tick_base >= last_rapid_crit_time + 1.0f || (i == 0 && test_bucket == bucket_cap)) {
          test_shots++;
          last_rapid_crit_time = tick_base;
        }
      }

      if (test_bucket < bucket_cap)
        test_bucket = std::min(test_bucket + base_damage, bucket_cap);

      next++;
    }
  }

  current_damage = base_damage;
  current_cost = cost * mult;
  potential_crits = potential;
  available_crits = available;
  next_crit = next;
}

void update_info(Player* local, Weapon* weapon) {
  update_weapon_info(local, weapon);

  crit_banned = false;
  damage_till_flip = 0.0f;
  if (!is_melee_weapon) {
    const float normalized_damage = static_cast<float>(crit_damage) / 3.0f;
    float current_crit_chance = crit_chance + 0.1f;
    if (ranged_damage && crit_damage) {
      const float observed_chance = normalized_damage / (normalized_damage + ranged_damage - crit_damage);
      crit_banned = observed_chance > current_crit_chance;
    }

    if (crit_banned)
      damage_till_flip = normalized_damage / current_crit_chance + normalized_damage * 2.0f - ranged_damage;
    else
      damage_till_flip = 3.0f * (normalized_damage - current_crit_chance * (normalized_damage + ranged_damage - crit_damage)) / (current_crit_chance - 1.0f);
  }

  auto* resource = get_player_resource_entity();
  if (resource != nullptr) {
    static const int damage_offset = tf2_netvars::find_offset("DT_TFPlayerResource", { "baseclass", "m_iDamage" });
    if (damage_offset > 0) {
      resource_damage = read_player_resource_value<int>(resource, damage_offset, engine->get_localplayer_index());
      desync_damage = ranged_damage + melee_damage - resource_damage;
    }
  }
}

crit_request get_crit_request(user_cmd* cmd, Weapon* weapon) {
  bool can_crit = available_crits > 0 && !crit_banned;
  bool pressed = config.crithack.force_crits || is_button_active(config.crithack.key);
  if (config.crithack.always_melee && is_melee_weapon) {
    pressed = true;
  }
  
  bool skip = config.crithack.avoid_random;
  bool desync = command_to_seed(cmd->command_number, weapon, is_melee_weapon) == weapon->current_seed();

  return can_crit && pressed ? crit_request::crit : (skip || desync ? crit_request::skip : crit_request::any);
}

} // namespace

create_move_result on_create_move(user_cmd* cmd) {
  create_move_result result{};

  auto* local = entity_list->get_localplayer();
  if (!config.crithack.enabled || local == nullptr || !local->is_alive() || local->is_dormant()) {
    return result;
  }

  auto* weapon = local->get_weapon();
  if (weapon == nullptr || !weapon_can_crit(weapon)) {
    return result;
  }

  update_info(local, weapon);

  if (local->is_crit_boosted() || weapon->crit_time() > global_vars->curtime) {
    return result;
  }

  // store health history for all alive players
  for (int i = 1; i <= global_vars->max_clients; i++) {
    auto* player = entity_list->player_from_index(i);
    if (player && player->is_alive() && !player->is_dormant()) {
      store_health_history(i, player->get_health(), player);
    }
  }

  bool attacking = (cmd->buttons & IN_ATTACK);
  if (is_melee_weapon) {
    attacking = weapon->can_primary_attack() && (cmd->buttons & IN_ATTACK);
    if (!attacking && weapon->get_weapon_id() == TF_WEAPON_FISTS) {
      attacking = weapon->can_primary_attack() && (cmd->buttons & IN_ATTACK2);
    }
  } else if (weapon->get_weapon_id() == TF_WEAPON_MINIGUN) {
    // Miniguns require active spin checking
    if (!(cmd->buttons & IN_ATTACK)) {
      attacking = false;
    }
  }

  if (!attacking || (weapon->is_rapid_fire() && global_vars->curtime < weapon->last_rapid_fire_crit_check_time() + 1.0f)) {
    return result;
  }

  crit_request req = get_crit_request(cmd, weapon);
  if (req == crit_request::any) {
    result.attack_allowed = true;
    return result;
  }

  const bool wants_crit = req == crit_request::crit;
  result.crit_requested = wants_crit;
  result.skip_requested = req == crit_request::skip;

  if (!is_crit_command(cmd->command_number, weapon, wants_crit, true, is_melee_weapon)) {
    cmd->buttons &= ~IN_ATTACK;
    if (is_melee_weapon && weapon->get_weapon_id() == TF_WEAPON_FISTS) {
      cmd->buttons &= ~IN_ATTACK2;
    }
    result.attack_suppressed = true;
    return result;
  }

  result.attack_allowed = true;
  return result;
}

void on_game_event(GameEvent* event) {
  if (event == nullptr) return;

  const char* event_name_ptr = event->get_name();
  if (event_name_ptr == nullptr) return;
  std::string event_name{ event_name_ptr };

  auto* local = entity_list->get_localplayer();

  if (event_name == "player_hurt") {
    if (local == nullptr) return;

    int victim_id = event->get_int("userid");
    int attacker_id = event->get_int("attacker");
    bool crit = event->get_bool("crit") || event->get_bool("minicrit");
    int damage = event->get_int("damageamount");
    int health = event->get_int("health");
    int weapon_id = event->get_int("weaponid");

    auto* victim = entity_list->get_player_from_id(victim_id);
    auto* attacker = entity_list->get_player_from_id(attacker_id);

    if (victim != nullptr) {
      int victim_idx = victim->get_index();
      if (health_history.count(victim_idx)) {
        auto& history = health_history[victim_idx];
        if (!health) {
          damage = std::clamp(damage, 0, history.new_health);
          history.spawn_counter = -1;
        } else {
          // Feign death checks if applicable
          if (victim->in_cond(TF_COND_FEIGN_DEATH)) {
            int old_h = (history.history_map.count(health) ? history.history_map[health].old_health : history.new_health) % 32768;
            if (health > old_h) {
              for (const auto& [h, storage] : history.history_map) {
                int old_h2 = storage.old_health % 32768;
                if (old_h2 > health) {
                  old_h = health > old_h ? old_h2 : std::min(old_h, old_h2);
                }
              }
            }
            damage = std::clamp(old_h - health, 0, damage);
          }
        }
      }
      if (health) {
        store_health_history(victim_idx, health, victim);
      }
    }

    if (attacker == nullptr || attacker != local || victim == attacker) {
      return;
    }

    // Ignore huge damages or competitive round states if needed
    const char* level_name = engine->get_level_name();
    bool is_mvm = level_name != nullptr && std::strstr(level_name, "mvm_") != nullptr;
    const int max_acceptable_damage = is_mvm ? 5000 : 1500;
    if (damage > max_acceptable_damage) {
      return;
    }

    Weapon* weapon = nullptr;
    for (int i = 0; i < 48; i++) {
      auto* weapon2 = local->get_weapon_at(i);
      if (weapon2 && weapon2->get_weapon_id() == weapon_id) {
        weapon = weapon2;
        break;
      }
    }

    if (weapon == nullptr || !weapon->is_melee()) {
      ranged_damage += damage;
      if (crit && !local->is_crit_boosted()) {
        crit_damage += damage;
      }
    } else {
      melee_damage += damage;
    }
  } else if (event_name == "player_spawn") {
    int victim_id = event->get_int("userid");
    auto* victim = entity_list->get_player_from_id(victim_id);
    if (victim != nullptr) {
      int victim_idx = victim->get_index();
      if (health_history.count(victim_idx)) {
        health_history[victim_idx].spawn_counter = -1;
      }
    }
  } else if (event_name == "scorestats_accumulated_update" || event_name == "mvm_reset_stats") {
    ranged_damage = crit_damage = melee_damage = 0;
  } else if (event_name == "client_beginconnect" || event_name == "client_disconnect" || event_name == "game_newmap") {
    reset();
  }
}

void reset() {
  crit_damage = 0;
  ranged_damage = 0;
  melee_damage = 0;
  resource_damage = 0;
  desync_damage = 0;
  crit_banned = false;
  damage_till_flip = 0.0f;
  health_history.clear();
}

void store_health_history(int index, int health, Player* player) {
  bool contains = health_history.count(index) > 0;
  auto& history = health_history[index];

  if (contains && player != nullptr) {
    if (player->is_dormant()) {
      history.spawn_counter = -1;
    } else {
      static const int spawn_counter_offset = tf2_netvars::find_offset("DT_BasePlayer", { "m_iSpawnCounter" });
      if (spawn_counter_offset > 0) {
        int sc = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(player) + spawn_counter_offset);
        if (history.spawn_counter == -1)
          history.spawn_counter = sc;
        else if (history.spawn_counter != sc)
          return;
      }
    }
  }

  if (!contains) {
    history.new_health = health;
    history.old_health = health;
    history.spawn_counter = -1;
  } else if (health != history.new_health) {
    history.old_health = std::max(history.new_health, health);
    history.new_health = health;
  }

  history.history_map[health % 32768] = { history.old_health, global_vars->curtime };

  while (history.history_map.size() > 3) {
    int oldest_health = 0;
    float min_time = std::numeric_limits<float>::max();
    for (const auto& [h, storage] : history.history_map) {
      if (storage.time < min_time) {
        min_time = storage.time;
        oldest_health = h;
      }
    }
    history.history_map.erase(oldest_health);
  }
}

bool weapon_can_crit(Weapon* weapon, bool weapon_only) {
  if (weapon == nullptr) return false;
  if (!weapon_only && !weapon->are_random_crits_enabled()) return false;
  if (attribute_manager->attrib_hook_value(1.0f, "mult_crit_chance", weapon->to_entity()) <= 0.0f) return false;

  switch (weapon->get_weapon_id()) {
    case TF_WEAPON_PDA:
    case TF_WEAPON_PDA_ENGINEER_BUILD:
    case TF_WEAPON_PDA_ENGINEER_DESTROY:
    case TF_WEAPON_PDA_SPY:
    case TF_WEAPON_PDA_SPY_BUILD:
    case TF_WEAPON_BUILDER:
    case TF_WEAPON_INVIS:
    case TF_WEAPON_JAR_MILK:
    case TF_WEAPON_LUNCHBOX:
    case TF_WEAPON_BUFF_ITEM:
    case TF_WEAPON_LASER_POINTER:
    case TF_WEAPON_MEDIGUN:
    case TF_WEAPON_SNIPERRIFLE:
    case TF_WEAPON_SNIPERRIFLE_DECAP:
    case TF_WEAPON_SNIPERRIFLE_CLASSIC:
    case TF_WEAPON_COMPOUND_BOW:
    case TF_WEAPON_JAR:
    case TF_WEAPON_KNIFE:
    case TF_WEAPON_PASSTIME_GUN:
      return false;
  }

  return true;
}

int predict_command_number(user_cmd* cmd) {
  return cmd != nullptr ? cmd->command_number : 0;
}

crit_stats_t get_stats() {
  crit_stats_t stats;
  stats.damage = current_damage;
  stats.cost = current_cost;
  stats.available = available_crits;
  stats.potential = potential_crits;
  stats.next = next_crit;
  stats.banned = crit_banned;
  stats.damage_till_flip = damage_till_flip;
  return stats;
}

} // namespace crit_hack
