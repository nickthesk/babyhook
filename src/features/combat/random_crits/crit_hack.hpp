#ifndef CRIT_HACK_HPP
#define CRIT_HACK_HPP

#include "core/types.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"

#include "imgui/dearimgui.hpp"

#include <unordered_map>

struct user_cmd;

namespace crit_hack {

struct crit_stats_t {
  float damage = 0.0f;
  float cost = 0.0f;
  int available = 0;
  int potential = 0;
  int next = 0;
  bool banned = false;
  float damage_till_flip = 0.0f;
};

struct health_storage_t {
  int old_health = 0;
  float time = 0.0f;
};

struct health_history_t {
  int new_health = 0;
  int old_health = 0;
  int spawn_counter = -1;
  std::unordered_map<int, health_storage_t> history_map{};
};

struct create_move_result {
  bool attack_suppressed = false;
  bool attack_allowed = false;
  bool crit_requested = false;
  bool skip_requested = false;
};

[[nodiscard]] create_move_result on_create_move(user_cmd* cmd);
void on_game_event(GameEvent* event);
void reset();
void store_health_history(int index, int health, Player* player = nullptr);
[[nodiscard]] bool weapon_can_crit(Weapon* weapon, bool weapon_only = false);
[[nodiscard]] int predict_command_number(user_cmd* cmd);
[[nodiscard]] crit_stats_t get_stats();

} // namespace crit_hack

#endif // CRIT_HACK_HPP
