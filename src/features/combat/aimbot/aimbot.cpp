#include "aimbot.hpp"

#include "aim_state.hpp"
#include "aim_spread.hpp"
#include "aim_auto_shoot.hpp"
#include "aim_walk.hpp"
#include "aim_targeting.hpp"
#include "aim_utils.hpp"
#include "aimbot_debug.hpp"
#include "hitscan_aim.hpp"
#include "melee_aim.hpp"
#include "proj_aim.hpp"
#include "resolver.hpp"

#include "core/entity_cache.hpp"
#include "features/combat/backtrack/backtrack.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"

namespace aimbot
{
namespace
{

struct fire_readiness {
  bool attack = false;
  bool headshot = true;
  bool charge = true;
  bool trace = true;
  bool settled = true;
  bool primary = true;

  bool ready() const {
    return attack && headshot && charge && trace && settled && primary;
  }
};

struct aimbot_run_context {
  user_cmd* cmd = nullptr;
  Player* local = nullptr;
  Weapon* weapon = nullptr;
  Vec3 source_angles{};
  Vec3 target_angles{};
  Vec3 applied_angles{};
  Vec3 projectile_angles{};
  aimbot_candidate target{};
  aimbot_debug_state debug{};
  aim_spread::hitscan_fire_solution hitscan_fire{};
  aim_auto_shoot::result auto_shoot{};
  fire_readiness readiness{};
  bool hitscan = false;
  bool projectile = false;
  bool melee = false;
  bool psilent = false;

  aimbot_run_result finish(aimbot_debug_reason reason) {
    aimbot_state& state = current_state();
    debug.reason = reason;
    debug.requested_shot = state.requested_shot;
    aimbot_debug_set_state(debug);
    store_input_angles(cmd != nullptr ? cmd->view_angles : source_angles);
    return {
      .psilent_command = psilent,
      .requested_shot = state.requested_shot,
      .active_target = state.active_target
    };
  }
};

bool weapon_allows_primary_fire(Player* localplayer, Weapon* weapon) {
  return localplayer != nullptr && weapon != nullptr &&
    (localplayer->is_scoped() || weapon->get_def_id() != Sniper_m_TheMachina);
}

bool melee_swing_active(Player* localplayer, Weapon* weapon) {
  if (localplayer == nullptr || weapon == nullptr || !aimbot_is_melee_weapon(weapon)) {
    return false;
  }

  constexpr float max_active_swing_time = 0.5f;
  const float current_time = global_vars != nullptr
    ? global_vars->curtime
    : localplayer->get_tickbase() * static_cast<float>(TICK_INTERVAL);
  const float smack_time = weapon->get_smack_time();
  return smack_time > current_time && smack_time - current_time <= max_active_swing_time;
}

void apply_visible_view(user_cmd* cmd) {
  if (cmd == nullptr || config.aimbot.aim_mode == Aim::AimMode::PSILENT) {
    return;
  }

  Vec3 angles = cmd->view_angles;
  if (prediction != nullptr) {
    prediction->set_local_view_angles(angles);
    prediction->set_view_angles(angles);
  }
  if (engine != nullptr) {
    engine->set_view_angles(angles);
  }
}

void reset_all_state() {
  clear_target_state();
  reset_autoscope_state();
  reset_aimbot_scope_timing();
}

void clear_invalid_state(Player* localplayer) {
  aimbot_state& state = current_state();
  if (!state.active_target) {
    state.target_player = nullptr;
    state.target_entity = nullptr;
  }

  if (active_target_player() != nullptr &&
      (!entity_cache_snapshot_contains_player(active_target_player()) ||
        aimbot_should_skip_player(localplayer, active_target_player()))) {
    state.target_player = nullptr;
    state.target_entity = nullptr;
    state.active_target = false;
  }

  if (state.preference.player != nullptr &&
      (!entity_cache_snapshot_contains_player(state.preference.player) ||
        aimbot_should_skip_player(localplayer, state.preference.player))) {
    clear_preference();
  }

  if (state.active_target &&
      state.target_player == nullptr &&
      state.target_entity != nullptr &&
      state.target_entity->get_class_id() == class_id::PLAYER) {
    state.target_entity = nullptr;
    state.active_target = false;
  }
}

Vec3 candidate_command_angles(Player* localplayer, const aimbot_candidate& candidate) {
  if (localplayer == nullptr) {
    return candidate.command_angles;
  }
  if ((candidate.projectile_direct || candidate.projectile_splash) && aimbot_vec3_is_finite(candidate.aim_angles)) {
    return candidate.aim_angles;
  }
  if (candidate.player != nullptr && aimbot_vec3_is_finite(candidate.command_angles)) {
    return candidate.command_angles;
  }
  return candidate.aim_angles - localplayer->get_punch_angles();
}

void populate_debug(aimbot_run_context& ctx) {
  aimbot_debug_state& debug = ctx.debug;
  debug.candidates_total = aim_state::scan.candidates_total;
  debug.candidates_visible = aim_state::scan.candidates_visible;
  debug.candidates_rejected = aim_state::scan.candidates_rejected;
  debug.skipped_ignored = aim_state::scan.skipped_ignored;
  debug.skipped_friends = aim_state::scan.skipped_friends;
  debug.skipped_ipc = aim_state::scan.skipped_ipc;
  debug.skipped_cloaked = aim_state::scan.skipped_cloaked;
  debug.skipped_team = aim_state::scan.skipped_team;
  debug.skipped_invulnerable = aim_state::scan.skipped_invulnerable;
  debug.skipped_dead = aim_state::scan.skipped_dead;
  debug.skipped_type = aim_state::scan.skipped_type;
  debug.last_reject = aim_state::scan.last_reject;
  debug.best_reject = aim_state::scan.best_reject;

  if (ctx.target.entity == nullptr) {
    return;
  }

  debug.selected_entity_index = ctx.target.entity->get_index();
  debug.selected_hitbox = ctx.target.hitbox;
  debug.fov = ctx.target.fov;
  debug.distance = ctx.target.distance;
  debug.tick_count = ctx.target.tick_count;

  if (ctx.target.player == nullptr) {
    return;
  }

  const resolver::resolver_debug_info info = resolver::debug_for_player(ctx.target.player);
  debug.resolver_active = info.active;
  debug.resolver_candidates = info.yaw_candidates;
  debug.resolver_misses = info.misses;
  debug.resolver_hits = info.hits;
  debug.resolver_yaw = info.yaw;
  debug.resolver_pitch = info.pitch;
  debug.resolver_mode = static_cast<int>(info.mode);
}

bool hitscan_fast_head_backtrack_better(const aimbot_candidate& candidate, const aimbot_candidate& best);
bool hitscan_ready_candidate_better(const aimbot_candidate& candidate, const aimbot_candidate& best);

aimbot_candidate find_best_hitscan_target(Player* localplayer,
  Weapon* weapon,
  user_cmd* cmd,
  const Vec3& view_angles) {
  aimbot_candidate best{};
  aimbot_candidate best_ready{};
  aim_state::scan = {};

  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    Player* player = entry.player;
    ++aim_state::scan.candidates_total;

    const aimbot_player_skip_reason skip_reason = aimbot_player_skip_reason_for(localplayer, player);
    if (skip_reason != aimbot_player_skip_reason::none) {
      aim_state::record_player_skip(skip_reason, player);
      continue;
    }

    const aimbot_candidate current_candidate = hitscan_aim_find_candidate(localplayer, weapon, player, view_angles);
    const aimbot_candidate backtrack_candidate = backtrack::find_hitscan_candidate(
      localplayer,
      weapon,
      player,
      view_angles,
      has_preference(player));
    aimbot_candidate candidate = current_candidate;
    if (aimbot_candidate_better(backtrack_candidate, candidate)) {
      candidate = backtrack_candidate;
    }

    if (candidate.entity == nullptr) {
      const aimbot_reject_debug reject = candidate.reject_debug.reason != aimbot_reject_reason::none
        ? candidate.reject_debug
        : aim_state::make_reject_debug(player, aimbot_reject_reason::no_candidate);
      aim_state::record_reject(reject);
      continue;
    }

    ++aim_state::scan.candidates_visible;
    const float fov_limit = aimbot_fov_limit(candidate.preferred ? 1.35f : 1.0f);
    if (candidate.fov > fov_limit) {
      aim_state::record_reject(aim_state::make_candidate_reject_debug(candidate, aimbot_reject_reason::fov, fov_limit));
      continue;
    }

    if (aimbot_candidate_better(candidate, best)) {
      best = candidate;
    }

    for (const aimbot_candidate& ready_candidate : { current_candidate, backtrack_candidate }) {
      if (ready_candidate.entity == nullptr ||
          !aimbot_fov_within_limit(ready_candidate.fov, ready_candidate.preferred ? 1.35f : 1.0f)) {
        continue;
      }

      if (aim_spread::hitscan_candidate_ready_for_selection(localplayer, weapon, cmd, ready_candidate) &&
          hitscan_ready_candidate_better(ready_candidate, best_ready)) {
        best_ready = ready_candidate;
      }
    }
  }

  const aimbot_candidate non_player = aim_targeting::find_best_non_player_candidate(localplayer, weapon, view_angles);
  if (aimbot_candidate_better(non_player, best)) {
    best = non_player;
  }

  if (best_ready.entity != nullptr &&
      best.player != nullptr &&
      (!aim_spread::hitscan_candidate_ready_for_selection(localplayer, weapon, cmd, best) ||
        hitscan_fast_head_backtrack_better(best_ready, best))) {
    best = best_ready;
  }

  return best;
}

aimbot_candidate find_best_melee_target(Player* localplayer,
  Weapon* weapon,
  user_cmd* cmd,
  const Vec3& view_angles) {
  return aim_targeting::find_best_candidate(localplayer, weapon, cmd, view_angles);
}

aimbot_candidate find_best_projectile_target(Player* localplayer,
  Weapon* weapon,
  user_cmd* cmd,
  const Vec3& view_angles) {
  aimbot_candidate best = aim_targeting::find_best_projectile_candidate(localplayer, weapon, cmd, view_angles);
  if (best.projectile_direct) {
    return best;
  }
  const aimbot_candidate non_player = aim_targeting::find_best_non_player_candidate(localplayer, weapon, view_angles);
  if (aimbot_candidate_better(non_player, best)) {
    best = non_player;
  }
  return best;
}

void find_target(aimbot_run_context& ctx) {
  if (ctx.projectile) {
    ctx.target = find_best_projectile_target(ctx.local, ctx.weapon, ctx.cmd, ctx.source_angles);
  } else if (ctx.melee) {
    ctx.target = find_best_melee_target(ctx.local, ctx.weapon, ctx.cmd, ctx.source_angles);
  } else {
    ctx.target = find_best_hitscan_target(ctx.local, ctx.weapon, ctx.cmd, ctx.source_angles);
  }

  set_active_target(ctx.target.entity, ctx.target.player);
  populate_debug(ctx);
}

bool melee_ready(const aimbot_run_context& ctx) {
  if (!ctx.melee) {
    return true;
  }
  if (ctx.target.player != nullptr) {
    return melee_aim_ready_candidate(ctx.local, ctx.weapon, ctx.target.player, ctx.target, ctx.cmd->view_angles);
  }
  return ctx.target.entity != nullptr &&
    aimbot_entity_melee_reachable(ctx.local, ctx.weapon, ctx.target.entity, ctx.cmd->view_angles);
}

bool projectile_ready(const aimbot_run_context& ctx) {
  if (!ctx.projectile) {
    return true;
  }
  if (!ctx.target.projectile_direct && !ctx.target.projectile_splash) {
    return true;
  }
  if (ctx.target.projectile_direct) {
    return aim_targeting::projectile_solution_ready(ctx.local, ctx.weapon, ctx.cmd, ctx.target, ctx.projectile_angles);
  }
  if (!aimbot_mode_uses_visible_steering()) {
    return aim_targeting::projectile_solution_ready(ctx.local, ctx.weapon, ctx.cmd, ctx.target, ctx.projectile_angles);
  }
  if (aimbot_calculate_fov(ctx.projectile_angles, ctx.cmd->view_angles) > aimbot_projectile_visible_settle_fov(ctx.target)) {
    return false;
  }
  return aim_targeting::projectile_solution_ready(ctx.local, ctx.weapon, ctx.cmd, ctx.target, ctx.projectile_angles);
}

bool hitscan_settled(const aimbot_run_context& ctx) {
  if (!ctx.hitscan || !aimbot_mode_uses_visible_steering()) {
    return true;
  }

  return hitscan_aim_trace_candidate(ctx.local, ctx.target, ctx.applied_angles);
}

bool hitscan_fast_head_backtrack_better(const aimbot_candidate& candidate, const aimbot_candidate& best) {
  if (!candidate.backtrack ||
      best.entity == nullptr ||
      best.backtrack ||
      candidate.player == nullptr ||
      candidate.player != best.player ||
      candidate.hitbox != aim_hitbox_head ||
      best.hitbox != aim_hitbox_head) {
    return false;
  }

  const float target_speed = aimbot_candidate_target_speed(candidate);
  return target_speed >= 360.0f && candidate.fov <= best.fov + 1.5f;
}

bool hitscan_ready_candidate_better(const aimbot_candidate& candidate, const aimbot_candidate& best) {
  return hitscan_fast_head_backtrack_better(candidate, best) || aimbot_candidate_better(candidate, best);
}

void compute_angles(aimbot_run_context& ctx) {
  aimbot_state& state = current_state();
  ctx.target_angles = candidate_command_angles(ctx.local, ctx.target);
  ctx.applied_angles = aimbot_apply_mode_angles(
    ctx.source_angles,
    ctx.target_angles,
    state.last_input_angles,
    state.last_input_angles_valid,
    ctx.target);
  ctx.cmd->view_angles = ctx.applied_angles;

  ctx.projectile_angles = ctx.projectile
    ? aim_spread::apply_projectile_random_compensation(ctx.local, ctx.weapon, ctx.cmd, ctx.target_angles)
    : ctx.target_angles;
}

void compute_hitscan_fire(aimbot_run_context& ctx) {
  if (!ctx.hitscan) {
    return;
  }

  ctx.hitscan_fire = aim_spread::prepare_hitscan_fire_solution(
    ctx.local,
    ctx.weapon,
    ctx.cmd,
    ctx.target,
    ctx.target_angles);

  if (ctx.hitscan_fire.ready) {
    ctx.target.command_angles = ctx.hitscan_fire.command_angles;
    ctx.target.spread_compensated = ctx.hitscan_fire.spread_compensated;
    ctx.target.pellet_index = ctx.hitscan_fire.pellet_index;
    ctx.target.pellet_count = ctx.hitscan_fire.pellet_count;
    ctx.target.spread = ctx.hitscan_fire.spread;
  }

  ctx.debug.final_trace_hit = ctx.hitscan_fire.ready;
  ctx.debug.spread_compensated = ctx.hitscan_fire.spread_compensated;
  ctx.debug.spread_signature = ctx.hitscan_fire.spread_signature;
  ctx.debug.spread_fixed = ctx.hitscan_fire.spread_fixed;
  ctx.debug.spread = ctx.hitscan_fire.spread;
  ctx.debug.pellet_count = ctx.hitscan_fire.pellet_count;
  ctx.debug.pellet_index = ctx.hitscan_fire.pellet_index;
  ctx.debug.trace_hitbox = ctx.hitscan_fire.trace_hitbox;
  ctx.debug.trace_entity_index = ctx.hitscan_fire.trace_entity_index;
}

void compute_readiness(aimbot_run_context& ctx) {
  ctx.readiness.headshot = !ctx.hitscan || hitscan_aim_headshot_ready(ctx.local, ctx.weapon, ctx.target);
  ctx.readiness.charge = !ctx.hitscan || hitscan_aim_charge_ready(ctx.local, ctx.weapon, ctx.target);
  ctx.readiness.trace = (!ctx.hitscan || ctx.hitscan_fire.ready) &&
    projectile_ready(ctx) &&
    melee_ready(ctx);
  ctx.readiness.settled = hitscan_settled(ctx);

  const bool secondary_blocks_attack =
    (ctx.cmd->buttons & IN_ATTACK2) != 0 &&
    !(ctx.projectile && aim_auto_shoot::weapon_should_clear_secondary(ctx.weapon));
  ctx.readiness.primary = weapon_allows_primary_fire(ctx.local, ctx.weapon) && !secondary_blocks_attack;
  ctx.readiness.attack = ctx.target.entity != nullptr &&
    (aim_auto_shoot::weapon_can_attack_or_release(ctx.local, ctx.weapon) ||
      melee_swing_active(ctx.local, ctx.weapon));
  if (ctx.projectile) {
    ctx.debug.final_trace_hit = ctx.readiness.trace;
  } else if (ctx.hitscan) {
    ctx.debug.final_trace_hit = ctx.hitscan_fire.ready && ctx.readiness.settled;
  }

  if (!ctx.readiness.ready()) {
    if (ctx.hitscan || ctx.melee) {
      ctx.cmd->buttons &= ~IN_ATTACK;
    }
  }

  ctx.debug.headshot_ready = ctx.readiness.headshot;
  ctx.debug.attack_ready = ctx.readiness.ready();
}

void apply_auto_shoot(aimbot_run_context& ctx) {
  if (config.aimbot.auto_shoot && ctx.readiness.ready()) {
    ctx.auto_shoot = aim_auto_shoot::apply(ctx.cmd, ctx.weapon, ctx.projectile, ctx.hitscan, ctx.melee);
    set_requested_shot(ctx.auto_shoot.requested);
    aim_state::requested_shot = ctx.auto_shoot.requested;
  }
}

void apply_fire_state(aimbot_run_context& ctx) {
  const bool firing = (ctx.cmd->buttons & IN_ATTACK) != 0 || ctx.auto_shoot.release_attack;
  const bool visible_steering = aimbot_mode_uses_visible_steering();

  if (ctx.target.player != nullptr) {
    if (ctx.readiness.ready()) {
      set_preference(ctx.target.player);
    } else if (current_state().preference.player == ctx.target.player && ctx.hitscan) {
      clear_preference();
    }
  }

  if (!ctx.readiness.ready()) {
    if (ctx.projectile) {
      aim_auto_shoot::hold_charge_if_needed(ctx.cmd, ctx.weapon);
    }
    if (config.aimbot.aim_mode == Aim::AimMode::PSILENT) {
      ctx.cmd->view_angles = ctx.source_angles;
    }
    apply_visible_view(ctx.cmd);
    return;
  }

  if (firing && visible_steering) {
    ctx.cmd->view_angles = ctx.applied_angles;
  } else if (firing && ctx.hitscan && ctx.hitscan_fire.ready) {
    ctx.cmd->view_angles = ctx.hitscan_fire.command_angles;
  } else if (firing && ctx.projectile) {
    ctx.cmd->view_angles = ctx.projectile_angles;
  } else if (firing) {
    ctx.cmd->view_angles = ctx.target_angles;
  }

  if (firing && ctx.hitscan && ctx.hitscan_fire.ready && ctx.target.player != nullptr) {
    resolver::note_shot(ctx.target.player, ctx.target.hitbox, ctx.target.simulation_time, ctx.target.backtrack);
  }

  if (firing && ctx.target.player != nullptr && ctx.hitscan && ctx.target.backtrack && ctx.target.tick_count > 0) {
    ctx.cmd->tick_count = ctx.target.tick_count;
  }

  ctx.psilent = config.aimbot.aim_mode == Aim::AimMode::PSILENT && firing;
  if (config.aimbot.aim_mode == Aim::AimMode::PSILENT && !ctx.psilent) {
    ctx.cmd->view_angles = ctx.source_angles;
  }

  apply_visible_view(ctx.cmd);
}

aimbot_debug_reason classify_outcome(const aimbot_run_context& ctx) {
  if (ctx.target.entity == nullptr) {
    return aimbot_debug_reason::no_target;
  }
  if (!ctx.readiness.headshot) {
    return aimbot_debug_reason::headshot_wait;
  }
  if ((ctx.hitscan || ctx.projectile) && !ctx.readiness.trace) {
    if (ctx.hitscan_fire.seed_missing) {
      return aimbot_debug_reason::spread_seed_missing;
    }
    if (ctx.hitscan_fire.hit_wrong_hitbox) {
      return aimbot_debug_reason::hitbox_miss;
    }
    return aimbot_debug_reason::final_trace_miss;
  }
  if (!ctx.readiness.ready()) {
    return aimbot_debug_reason::attack_not_ready;
  }
  return aimbot_debug_reason::attack_ready;
}

bool validate_context(aimbot_run_context& ctx) {
  if (!config.aimbot.master) {
    reset_all_state();
    ctx.finish(aimbot_debug_reason::disabled);
    return false;
  }

  ctx.local = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (ctx.local == nullptr || !ctx.local->is_alive()) {
    reset_all_state();
    ctx.finish(aimbot_debug_reason::no_localplayer);
    return false;
  }

  update_aimbot_scope_timing(ctx.local);
  ctx.weapon = ctx.local->get_weapon();
  if (ctx.weapon == nullptr) {
    clear_target_state();
    reset_autoscope_state();
    reset_aimbot_scope_timing();
    ctx.finish(aimbot_debug_reason::no_weapon);
    return false;
  }

  ctx.debug.weapon_def_id = ctx.weapon->get_def_id();
  ctx.projectile = aimbot_is_projectile_weapon(ctx.weapon);
  ctx.melee = aimbot_is_melee_weapon(ctx.weapon);
  ctx.hitscan = !ctx.projectile && !ctx.melee;
  return true;
}

}

aimbot_run_result run(user_cmd* cmd, const Vec3& original_view_angles) {
  aim_state::requested_shot = false;
  clear_frame_target();

  aimbot_run_context ctx{};
  ctx.cmd = cmd;
  ctx.source_angles = original_view_angles;
  ctx.debug.active = config.aimbot.master;
  ctx.debug.aim_mode = static_cast<int>(config.aimbot.aim_mode);

  if (cmd == nullptr) {
    return ctx.finish(aimbot_debug_reason::disabled);
  }

  if (!validate_context(ctx)) {
    return {
      .psilent_command = false,
      .requested_shot = current_state().requested_shot,
      .active_target = current_state().active_target
    };
  }

  clear_invalid_state(ctx.local);

  if (!aimbot_use_key_active()) {
    return ctx.finish(aimbot_debug_reason::use_key_inactive);
  }

  find_target(ctx);

  if (aimbot_should_auto_unscope(ctx.local, ctx.weapon, ctx.target) ||
      aimbot_should_auto_unrev(ctx.local, ctx.weapon, ctx.target)) {
    ctx.cmd->buttons |= IN_ATTACK2;
  }

  if (ctx.target.entity == nullptr) {
    aim_auto_shoot::hold_charge_if_needed(ctx.cmd, ctx.weapon);
    return ctx.finish(aimbot_debug_reason::no_target);
  }

  ctx.cmd->buttons &= ~IN_RELOAD;

  if (aimbot_should_auto_scope(ctx.local, ctx.weapon, ctx.target)) {
    ctx.cmd->buttons |= IN_ATTACK2;
    ctx.cmd->buttons &= ~IN_ATTACK;
    return ctx.finish(aimbot_debug_reason::auto_scope);
  }

  if (aimbot_should_auto_rev(ctx.local, ctx.weapon, ctx.target)) {
    ctx.cmd->buttons |= IN_ATTACK2;
    ctx.cmd->buttons &= ~IN_ATTACK;
    return ctx.finish(aimbot_debug_reason::auto_rev);
  }

  if (!aimbot_scoped_only_ready(ctx.local, ctx.weapon)) {
    ctx.cmd->buttons &= ~IN_ATTACK;
    if (ctx.target.player != nullptr && current_state().preference.player == ctx.target.player) {
      clear_preference();
    }
    aim_auto_shoot::hold_charge_if_needed(ctx.cmd, ctx.weapon);
    ctx.debug.scoped = ctx.local->is_scoped();
    ctx.debug.scoped_ready = false;
    return ctx.finish(aimbot_debug_reason::scoped_only);
  }

  aim_walk::request(ctx.local, ctx.weapon, ctx.target);
  compute_angles(ctx);
  compute_hitscan_fire(ctx);
  compute_readiness(ctx);
  apply_auto_shoot(ctx);
  apply_fire_state(ctx);

  ctx.debug.scoped = ctx.local->is_scoped();
  ctx.debug.scoped_ready = true;
  return ctx.finish(classify_outcome(ctx));
}

void apply_walk_to_target(Player* localplayer, user_cmd* cmd) {
  aim_walk::apply(localplayer, cmd);
}

}
