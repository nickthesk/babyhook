/*
/^-----^\   data: 2026-05-09
V  o o  V  file: src/features/combat/aimbot/aimbot_debug.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef AIMBOT_DEBUG_HPP
#define AIMBOT_DEBUG_HPP

#include <cfloat>

enum class aimbot_debug_reason {
  none,
  disabled,
  no_localplayer,
  no_weapon,
  no_target,
  use_key_inactive,
  auto_scope,
  auto_rev,
  attack_not_ready,
  scoped_only,
  headshot_wait,
  final_trace_miss,
  spread_seed_missing,
  attack_ready
};

struct aimbot_debug_state {
  bool active = false;
  bool requested_shot = false;
  bool attack_ready = false;
  bool scoped = false;
  bool scoped_ready = false;
  bool headshot_ready = false;
  bool final_trace_hit = false;
  bool spread_compensated = false;
  bool spread_signature = false;
  bool spread_fixed = false;
  int aim_mode = 0;
  int weapon_def_id = 0;
  int selected_entity_index = -1;
  int selected_hitbox = -1;
  int trace_entity_index = -1;
  int trace_hitbox = -1;
  int pellet_count = 0;
  int pellet_index = -1;
  int tick_count = 0;
  int candidates_total = 0;
  int candidates_visible = 0;
  int candidates_rejected = 0;
  float fov = FLT_MAX;
  float distance = FLT_MAX;
  float spread = 0.0f;
  aimbot_debug_reason reason = aimbot_debug_reason::none;
};

inline static aimbot_debug_state aimbot_debug_current_state{};

inline void aimbot_debug_set_state(const aimbot_debug_state& state) {
  aimbot_debug_current_state = state;
}

inline const aimbot_debug_state& aimbot_debug_get_state() {
  return aimbot_debug_current_state;
}

inline const char* aimbot_debug_reason_name(aimbot_debug_reason reason) {
  switch (reason) {
  case aimbot_debug_reason::none:
    return "none";
  case aimbot_debug_reason::disabled:
    return "disabled";
  case aimbot_debug_reason::no_localplayer:
    return "no local";
  case aimbot_debug_reason::no_weapon:
    return "no weapon";
  case aimbot_debug_reason::no_target:
    return "no target";
  case aimbot_debug_reason::use_key_inactive:
    return "key inactive";
  case aimbot_debug_reason::auto_scope:
    return "auto scope";
  case aimbot_debug_reason::auto_rev:
    return "auto rev";
  case aimbot_debug_reason::attack_not_ready:
    return "attack wait";
  case aimbot_debug_reason::scoped_only:
    return "scope wait";
  case aimbot_debug_reason::headshot_wait:
    return "head wait";
  case aimbot_debug_reason::final_trace_miss:
    return "trace miss";
  case aimbot_debug_reason::spread_seed_missing:
    return "spread seed";
  case aimbot_debug_reason::attack_ready:
    return "ready";
  }

  return "unknown";
}

#endif
