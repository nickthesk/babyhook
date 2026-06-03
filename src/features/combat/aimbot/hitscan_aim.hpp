#ifndef HITSCAN_AIM_HPP
#define HITSCAN_AIM_HPP

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>

#include "aimbot.hpp"
#include "aim_utils.hpp"

#include "features/combat/backtrack/backtrack.hpp"

struct hitscan_settings_view {
  uint32_t hitbox_mask = aim_hitbox_mask_default_hitscan;
  bool scoped_only = false;
  bool wait_for_headshot = false;
  bool wait_for_charge = false;
  bool body_aim_if_lethal = false;
  bool spread_compensation = false;
  float multipoint_scale = 0.0f;
};

struct hitscan_point {
  bool valid = false;
  int bone = 0;
  int hitbox = -1;
  int studio_hitbox = -1;
  int priority = 0;
  Vec3 position{};
  Vec3 angles{};
  float fov = FLT_MAX;
  aimbot_reject_debug reject_debug{};
};

struct hitscan_hitbox_entry {
  int hitbox = -1;
  int studio_hitbox = -1;
  int priority = 0;
};

struct hitscan_target {
  aimbot_candidate candidate{};
  bool ready = false;
};

struct hitscan_trace_result {
  bool hit = false;
  bool clear = false;
  Entity* entity = nullptr;
  int hitbox = -1;
};

using hitscan_aim_trace_result = hitscan_trace_result;

constexpr int hitscan_aim_max_bones = aimbot_max_bones;
constexpr int hitscan_aim_bone_mask = aimbot_bone_mask;

inline bool hitscan_aim_same_entity(Entity* left, Entity* right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  if (left == right) {
    return true;
  }

  return left->get_index() == right->get_index();
}

inline aimbot_reject_debug hitscan_aim_make_reject_debug(Player* target,
  aimbot_reject_reason reason,
  float fov = FLT_MAX,
  float distance = FLT_MAX,
  int hitbox = -1,
  int trace_entity_index = -1,
  int trace_hitbox = -1) {
  aimbot_reject_debug debug{};
  debug.reason = reason;
  debug.fov = fov;
  debug.distance = distance;
  debug.hitbox = hitbox;
  debug.trace_entity_index = trace_entity_index;
  debug.trace_hitbox = trace_hitbox;

  if (target != nullptr) {
    debug.entity_index = target->get_index();
    debug.team = static_cast<int>(target->get_team());
    debug.health = target->get_health();
  }

  return debug;
}

inline bool hitscan_aim_reject_better(const aimbot_reject_debug& candidate, const aimbot_reject_debug& best) {
  if (candidate.reason == aimbot_reject_reason::none) {
    return false;
  }
  if (best.reason == aimbot_reject_reason::none) {
    return true;
  }

  const bool candidate_has_fov = std::isfinite(candidate.fov) && candidate.fov < FLT_MAX;
  const bool best_has_fov = std::isfinite(best.fov) && best.fov < FLT_MAX;
  if (candidate_has_fov != best_has_fov) {
    return candidate_has_fov;
  }
  if (candidate_has_fov && candidate.fov != best.fov) {
    return candidate.fov < best.fov;
  }

  return false;
}

inline void hitscan_aim_keep_reject(hitscan_point* point, const aimbot_reject_debug& reject) {
  if (point != nullptr && hitscan_aim_reject_better(reject, point->reject_debug)) {
    point->reject_debug = reject;
  }
}

inline bool hitscan_aim_setup_bones(Player* target, matrix_3x4* bone_to_world) {
  if (target == nullptr || bone_to_world == nullptr) {
    return false;
  }

  return aimbot_setup_bones(target, bone_to_world);
}

inline Vec3 hitscan_aim_bullet_angles(Player* localplayer, const Vec3& view_angles) {
  return localplayer != nullptr ? view_angles + localplayer->get_punch_angles() : view_angles;
}

inline Vec3 hitscan_aim_command_angles(Player* localplayer, const Vec3& bullet_angles) {
  return localplayer != nullptr ? bullet_angles - localplayer->get_punch_angles() : bullet_angles;
}

inline uint32_t hitscan_aim_configured_hitbox_mask() {
  const uint32_t mask = config.aimbot.hitscan_hitboxes & aim_hitbox_mask_all;
  return mask != 0 ? mask : aim_hitbox_mask_default_hitscan;
}

inline unsigned int hitscan_aim_trace_mask() {
  unsigned int trace_mask = MASK_SHOT | CONTENTS_GRATE;
  if (config.aimbot.shoot_through_glass) {
    trace_mask &= ~CONTENTS_WINDOW;
  }

  return trace_mask;
}

inline hitscan_settings_view hitscan_aim_settings() {
  return {
    .hitbox_mask = hitscan_aim_configured_hitbox_mask(),
    .scoped_only = aimbot_modifier_enabled(Aim::hitscan_mod_scoped_only),
    .wait_for_headshot = aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_headshot),
    .wait_for_charge = aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_charge),
    .body_aim_if_lethal = aimbot_modifier_enabled(Aim::hitscan_mod_body_aim_if_lethal),
    .spread_compensation = config.aimbot.spread_compensation,
    .multipoint_scale = std::clamp(config.aimbot.multipoint_scale, 0.0f, 100.0f)
  };
}

inline bool hitscan_aim_world_clear(const Vec3& start_pos, const Vec3& end_pos) {
  if (engine_trace == nullptr || !aimbot_vec3_is_finite(start_pos) || !aimbot_vec3_is_finite(end_pos)) {
    return false;
  }

  Vec3 start = start_pos;
  Vec3 end = end_pos;
  ray_t ray = engine_trace->init_ray(&start, &end);
  trace_filter filter{};
  engine_trace->init_world_trace_filter(&filter);
  trace_t trace{};
  engine_trace->trace_ray(&ray, hitscan_aim_trace_mask(), &filter, &trace);
  return !trace.all_solid && !trace.start_solid && trace.fraction >= 0.999f;
}

inline hitscan_trace_result hitscan_aim_trace_line(Player* localplayer,
  const Vec3& start_pos,
  const Vec3& end_pos,
  Entity* target = nullptr) {
  hitscan_trace_result result{};
  if (engine_trace == nullptr || localplayer == nullptr) {
    return result;
  }

  Vec3 start = start_pos;
  Vec3 end = end_pos;
  ray_t ray = engine_trace->init_ray(&start, &end);
  trace_filter filter{};
  engine_trace->init_hitscan_trace_filter(&filter, localplayer, target);
  trace_t trace{};
  engine_trace->trace_ray(&ray, hitscan_aim_trace_mask(), &filter, &trace);

  result.entity = static_cast<Entity*>(trace.entity);
  result.hitbox = trace.hitbox;
  result.clear = !trace.all_solid && !trace.start_solid && trace.fraction >= 0.999f;
  result.hit = result.entity != nullptr || result.clear;
  return result;
}

inline bool hitscan_aim_trace_point(Player* localplayer,
  Entity* target,
  const Vec3& point,
  hitscan_trace_result* result_out = nullptr) {
  if (localplayer == nullptr || target == nullptr || !aimbot_vec3_is_finite(point)) {
    return false;
  }

  const Vec3 start_pos = localplayer->get_shoot_pos();
  const Vec3 to_point = point - start_pos;
  const float distance = std::sqrt((to_point.x * to_point.x) + (to_point.y * to_point.y) + (to_point.z * to_point.z));
  if (distance <= 0.001f) {
    return false;
  }

  const Vec3 direction = to_point * (1.0f / distance);
  const float trace_length = std::max(distance + 16.0f, 128.0f);
  const Vec3 end_pos = start_pos + (direction * trace_length);
  hitscan_trace_result result = hitscan_aim_trace_line(localplayer, start_pos, end_pos, target);
  if (result_out != nullptr) {
    *result_out = result;
  }

  if (result.entity != nullptr) {
    return hitscan_aim_same_entity(result.entity, target);
  }

  if (target->get_class_id() == class_id::PLAYER) {
    return false;
  }

  return result.clear;
}

inline hitscan_point hitscan_aim_make_point(Player* localplayer,
  Player* target,
  Weapon* weapon,
  const Vec3& view_angles,
  int hitbox,
  int studio_hitbox,
  int bone,
  int priority,
  Vec3 position);

inline bool hitscan_aim_body_forced(Player* localplayer, Weapon* weapon, Player* target) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr) {
    return false;
  }

  if (!weapon->is_headshot_weapon()) {
    return true;
  }

  if (weapon->is_sniper_rifle() && !localplayer->is_scoped() && weapon->get_charged_damage() <= 0.0f) {
    return true;
  }

  if (aimbot_modifier_enabled(Aim::hitscan_mod_body_aim_if_lethal) &&
      aimbot_body_aim_lethal(localplayer, weapon, target)) {
    return true;
  }

  return false;
}

inline bool hitscan_aim_head_only(uint32_t hitbox_mask) {
  return (hitbox_mask & aim_hitbox_mask_head) != 0 && (hitbox_mask & ~aim_hitbox_mask_head) == 0;
}

inline int hitscan_aim_first_hitbox_for_mask(uint32_t mask, const std::array<int, 18>& order) {
  for (const int hitbox : order) {
    if (aimbot_hitbox_matches_mask(hitbox, mask)) {
      return hitbox;
    }
  }

  return -1;
}

inline int hitscan_aim_priority_hitbox(Player* localplayer,
  Weapon* weapon,
  Player* target,
  const hitscan_settings_view& settings) {
  constexpr std::array<int, 18> body_order{
    aim_hitbox_spine_3,
    aim_hitbox_spine_2,
    aim_hitbox_spine_1,
    aim_hitbox_spine_0,
    aim_hitbox_pelvis,
    aim_hitbox_left_upper_arm,
    aim_hitbox_right_upper_arm,
    aim_hitbox_left_thigh,
    aim_hitbox_right_thigh,
    aim_hitbox_left_forearm,
    aim_hitbox_right_forearm,
    aim_hitbox_left_calf,
    aim_hitbox_right_calf,
    aim_hitbox_left_hand,
    aim_hitbox_right_hand,
    aim_hitbox_left_foot,
    aim_hitbox_right_foot,
    aim_hitbox_head
  };
  constexpr std::array<int, 18> head_order{
    aim_hitbox_head,
    aim_hitbox_spine_3,
    aim_hitbox_spine_2,
    aim_hitbox_spine_1,
    aim_hitbox_spine_0,
    aim_hitbox_pelvis,
    aim_hitbox_left_upper_arm,
    aim_hitbox_right_upper_arm,
    aim_hitbox_left_thigh,
    aim_hitbox_right_thigh,
    aim_hitbox_left_forearm,
    aim_hitbox_right_forearm,
    aim_hitbox_left_calf,
    aim_hitbox_right_calf,
    aim_hitbox_left_hand,
    aim_hitbox_right_hand,
    aim_hitbox_left_foot,
    aim_hitbox_right_foot
  };

  if (!hitscan_aim_head_only(settings.hitbox_mask) && hitscan_aim_body_forced(localplayer, weapon, target)) {
    uint32_t body_mask = settings.hitbox_mask & ~aim_hitbox_mask_head;
    if (body_mask == 0) {
      body_mask = aim_hitbox_mask_body | aim_hitbox_mask_pelvis;
    }
    return hitscan_aim_first_hitbox_for_mask(body_mask, body_order);
  }

  const bool head_ready = aimbot_headshot_ready_for_priority(localplayer, weapon);
  if (weapon != nullptr &&
      weapon->is_headshot_weapon() &&
      (settings.wait_for_headshot || head_ready) &&
      (settings.hitbox_mask & aim_hitbox_mask_head) != 0) {
    return aim_hitbox_head;
  }

  return hitscan_aim_first_hitbox_for_mask(settings.hitbox_mask, body_order);
}

inline int hitscan_aim_build_hitbox_order(int priority_hitbox,
  const hitscan_settings_view& settings,
  std::array<int, 18>* order_out) {
  if (order_out == nullptr || priority_hitbox < 0) {
    return 0;
  }

  constexpr std::array<int, 18> fallback_order{
    aim_hitbox_head,
    aim_hitbox_spine_3,
    aim_hitbox_spine_2,
    aim_hitbox_spine_1,
    aim_hitbox_spine_0,
    aim_hitbox_pelvis,
    aim_hitbox_left_upper_arm,
    aim_hitbox_right_upper_arm,
    aim_hitbox_left_thigh,
    aim_hitbox_right_thigh,
    aim_hitbox_left_forearm,
    aim_hitbox_right_forearm,
    aim_hitbox_left_calf,
    aim_hitbox_right_calf,
    aim_hitbox_left_hand,
    aim_hitbox_right_hand,
    aim_hitbox_left_foot,
    aim_hitbox_right_foot
  };

  int count = 0;
  auto add_hitbox = [&](int hitbox) {
    if (!aimbot_hitbox_matches_mask(hitbox, settings.hitbox_mask)) {
      return;
    }
    for (int index = 0; index < count; ++index) {
      if ((*order_out)[index] == hitbox) {
        return;
      }
    }
    (*order_out)[count++] = hitbox;
  };

  add_hitbox(priority_hitbox);
  for (const int hitbox : fallback_order) {
    add_hitbox(hitbox);
  }
  return count;
}

inline bool hitscan_aim_accepts_trace_hitbox(const aimbot_candidate& candidate, int trace_hitbox) {
  if (candidate.player == nullptr || candidate.hitbox < 0) {
    return true;
  }

  const int base_hitbox = aimbot_studio_hitbox_to_base(candidate.player, trace_hitbox);
  if (base_hitbox < 0) {
    return false;
  }

  if (base_hitbox < 0) {
    return true;
  }

  if (candidate.hitbox == aim_hitbox_head) {
    return base_hitbox == aim_hitbox_head;
  }

  return base_hitbox == candidate.hitbox ||
    aimbot_hitbox_matches_mask(base_hitbox, hitscan_aim_configured_hitbox_mask());
}

inline bool hitscan_aim_ray_hits_model_hitbox(Player* target,
  int studio_hitbox_id,
  const Vec3& start_pos,
  const Vec3& end_pos) {
  if (target == nullptr || studio_hitbox_id < 0 || model_info == nullptr) {
    return false;
  }

  const model_t* model = target->get_model();
  studio_hdr* hdr = model != nullptr ? model_info->get_studio_model(model) : nullptr;
  studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(target->get_hitbox_set()) : nullptr;
  if (hitbox_set == nullptr || studio_hitbox_id >= hitbox_set->num_hitboxes) {
    return false;
  }

  studio_box* hitbox = hitbox_set->hitbox(studio_hitbox_id);
  if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= 128) {
    return false;
  }

  matrix_3x4 bone_to_world[hitscan_aim_max_bones]{};
  if (!hitscan_aim_setup_bones(target, bone_to_world)) {
    return false;
  }

  const Vec3 local_start = aimbot_inverse_transform_point(start_pos, bone_to_world[hitbox->bone]);
  const Vec3 local_end = aimbot_inverse_transform_point(end_pos, bone_to_world[hitbox->bone]);
  constexpr float expansion = 1.25f;
  const Vec3 mins = hitbox->bbmin - Vec3{expansion, expansion, expansion};
  const Vec3 maxs = hitbox->bbmax + Vec3{expansion, expansion, expansion};
  return aimbot_segment_intersects_aabb(local_start, local_end, mins, maxs);
}

inline bool hitscan_aim_ray_hits_player_bounds(Player* target,
  const Vec3& start_pos,
  const Vec3& end_pos) {
  if (target == nullptr) {
    return false;
  }

  const Vec3 origin = target->get_collision_origin();
  const Vec3 mins = target->get_collideable_mins() + origin - Vec3{2.0f, 2.0f, 2.0f};
  const Vec3 maxs = target->get_collideable_maxs() + origin + Vec3{2.0f, 2.0f, 2.0f};
  if (!aimbot_vec3_is_finite(mins) || !aimbot_vec3_is_finite(maxs)) {
    return false;
  }

  return aimbot_segment_intersects_aabb(start_pos, end_pos, mins, maxs);
}

inline bool hitscan_aim_ray_hits_entity_bounds(Entity* target,
  const Vec3& start_pos,
  const Vec3& end_pos) {
  if (target == nullptr) {
    return false;
  }

  const Vec3 origin = target->get_collision_origin();
  const Vec3 mins = target->get_collideable_mins() + origin - Vec3{2.0f, 2.0f, 2.0f};
  const Vec3 maxs = target->get_collideable_maxs() + origin + Vec3{2.0f, 2.0f, 2.0f};
  if (!aimbot_vec3_is_finite(mins) || !aimbot_vec3_is_finite(maxs)) {
    return false;
  }

  return aimbot_segment_intersects_aabb(start_pos, end_pos, mins, maxs);
}

inline bool hitscan_aim_trace_geometry(const aimbot_candidate& candidate,
  const Vec3& start_pos,
  const Vec3& end_pos) {
  if (candidate.entity == nullptr || !aimbot_vec3_is_finite(candidate.aim_position)) {
    return false;
  }

  if (!hitscan_aim_world_clear(start_pos, candidate.aim_position)) {
    return false;
  }

  if (candidate.player != nullptr) {
    if (candidate.hitbox >= 0) {
      const int studio_hitbox = candidate.studio_hitbox >= 0
        ? candidate.studio_hitbox
        : aimbot_base_hitbox_to_studio(candidate.player, candidate.hitbox);
      return hitscan_aim_ray_hits_model_hitbox(candidate.player, studio_hitbox, start_pos, end_pos) ||
        (candidate.hitbox != aim_hitbox_head && hitscan_aim_ray_hits_player_bounds(candidate.player, start_pos, end_pos));
    }

    return hitscan_aim_ray_hits_player_bounds(candidate.player, start_pos, end_pos);
  }

  return hitscan_aim_ray_hits_entity_bounds(candidate.entity, start_pos, end_pos);
}

inline hitscan_point hitscan_aim_make_point(Player* localplayer,
  Player* target,
  Weapon* weapon,
  const Vec3& view_angles,
  int hitbox,
  int studio_hitbox,
  int bone,
  int priority,
  Vec3 position) {
  hitscan_point point{};
  if (localplayer == nullptr || target == nullptr || !aimbot_vec3_is_finite(position)) {
    point.reject_debug = hitscan_aim_make_reject_debug(target, aimbot_reject_reason::invalid);
    return point;
  }

  const Vec3 start_pos = localplayer->get_shoot_pos();
  const Vec3 aim_angles = aimbot_calculate_angles_to_position(start_pos, position);
  const float fov = aimbot_calculate_fov(hitscan_aim_command_angles(localplayer, aim_angles), view_angles);
  hitscan_trace_result trace{};
  const bool trace_reaches_target = hitscan_aim_trace_point(localplayer, target, position, &trace);
  if (hitscan_aim_same_entity(trace.entity, target)) {
    if (trace.hitbox < 0) {
      point.valid = true;
      point.bone = bone;
      point.hitbox = hitbox;
      point.studio_hitbox = studio_hitbox;
      point.priority = priority;
      point.position = position;
      point.angles = aim_angles;
      point.fov = fov;
      return point;
    }

    const int base_hitbox = aimbot_studio_hitbox_to_base(target, trace.hitbox);
    if (hitbox == aim_hitbox_head && base_hitbox >= 0 && base_hitbox != aim_hitbox_head) {
      point.reject_debug = hitscan_aim_make_reject_debug(
        target,
        aimbot_reject_reason::wrong_hitbox,
        fov,
        distance_3d(localplayer->get_origin(), target->get_origin()),
        hitbox,
        target->get_index(),
        trace.hitbox);
      return point;
    }
  } else if (trace.entity != nullptr) {
    point.reject_debug = hitscan_aim_make_reject_debug(
      target,
      aimbot_reject_reason::trace_blocked,
      fov,
      distance_3d(localplayer->get_origin(), target->get_origin()),
      hitbox,
      trace.entity->get_index(),
      trace.hitbox);
    return point;
  }

  if (!trace_reaches_target && !hitscan_aim_world_clear(start_pos, position)) {
    point.reject_debug = hitscan_aim_make_reject_debug(
      target,
      aimbot_reject_reason::trace_blocked,
      fov,
      distance_3d(localplayer->get_origin(), target->get_origin()),
      hitbox,
      trace.entity != nullptr ? trace.entity->get_index() : -1,
      trace.hitbox);
    return point;
  }

  if (hitbox == aim_hitbox_head &&
      hitscan_aim_same_entity(trace.entity, target) &&
      trace.hitbox >= 0) {
    const int base_hitbox = aimbot_studio_hitbox_to_base(target, trace.hitbox);
    if (base_hitbox >= 0 && base_hitbox != aim_hitbox_head) {
      point.reject_debug = hitscan_aim_make_reject_debug(
        target,
        aimbot_reject_reason::wrong_hitbox,
        fov,
        distance_3d(localplayer->get_origin(), target->get_origin()),
        hitbox,
        target->get_index(),
        trace.hitbox);
      return point;
    }
  }

  point.valid = true;
  point.bone = bone;
  point.hitbox = hitbox;
  point.studio_hitbox = studio_hitbox;
  point.priority = priority;
  point.position = position;
  point.angles = aim_angles;
  point.fov = fov;
  return point;
}

inline int hitscan_aim_build_studio_hitbox_entries(Player* localplayer,
  Weapon* weapon,
  Player* target,
  studio_hitbox_set* hitbox_set,
  const hitscan_settings_view& settings,
  int priority_hitbox,
  hitscan_hitbox_entry* entries,
  int max_entries) {
  if (target == nullptr || hitbox_set == nullptr || entries == nullptr || max_entries <= 0) {
    return 0;
  }

  int count = 0;
  for (int studio_hitbox_id = 0; studio_hitbox_id < hitbox_set->num_hitboxes && count < max_entries; ++studio_hitbox_id) {
    studio_box* hitbox = hitbox_set->hitbox(studio_hitbox_id);
    if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= 128) {
      continue;
    }

    const int base_hitbox = aimbot_studio_hitbox_to_base(target, studio_hitbox_id);
    if (base_hitbox < 0 || !aimbot_hitbox_matches_mask(base_hitbox, settings.hitbox_mask)) {
      continue;
    }

    entries[count++] = {
      .hitbox = base_hitbox,
      .studio_hitbox = studio_hitbox_id,
      .priority = base_hitbox == priority_hitbox
        ? 0
        : aimbot_hitbox_priority(localplayer, target, weapon, base_hitbox) + 1
    };
  }

  std::sort(entries, entries + count, [](const hitscan_hitbox_entry& left, const hitscan_hitbox_entry& right) {
    if (left.priority != right.priority) {
      return left.priority < right.priority;
    }
    return left.studio_hitbox < right.studio_hitbox;
  });

  return count;
}

inline hitscan_point hitscan_aim_find_point(Player* localplayer,
  Weapon* weapon,
  Player* target,
  const Vec3& view_angles) {
  hitscan_point best{};
  if (localplayer == nullptr || weapon == nullptr || target == nullptr || model_info == nullptr) {
    best.reject_debug = hitscan_aim_make_reject_debug(target, aimbot_reject_reason::invalid);
    return best;
  }

  const hitscan_settings_view settings = hitscan_aim_settings();
  const int priority_hitbox = hitscan_aim_priority_hitbox(localplayer, weapon, target, settings);
  if (priority_hitbox < 0) {
    best.reject_debug = hitscan_aim_make_reject_debug(target, aimbot_reject_reason::no_hitbox);
    return best;
  }

  const model_t* model = target->get_model();
  if (model == nullptr) {
    best.reject_debug = hitscan_aim_make_reject_debug(target, aimbot_reject_reason::no_model);
    return best;
  }

  studio_hdr* hdr = model_info->get_studio_model(model);
  if (hdr == nullptr) {
    best.reject_debug = hitscan_aim_make_reject_debug(target, aimbot_reject_reason::no_studio_model);
    return best;
  }

  studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(target->get_hitbox_set()) : nullptr;
  if (hitbox_set == nullptr) {
    best.reject_debug = hitscan_aim_make_reject_debug(target, aimbot_reject_reason::no_hitbox_set);
    return best;
  }

  matrix_3x4 bone_to_world[hitscan_aim_max_bones]{};
  if (!hitscan_aim_setup_bones(target, bone_to_world)) {
    best.reject_debug = hitscan_aim_make_reject_debug(target, aimbot_reject_reason::setup_bones);
    return best;
  }

  const Vec3 shoot_pos = localplayer->get_shoot_pos();
  constexpr int max_entries = 32;
  hitscan_hitbox_entry entries[max_entries]{};
  const int entry_count = hitscan_aim_build_studio_hitbox_entries(
    localplayer,
    weapon,
    target,
    hitbox_set,
    settings,
    priority_hitbox,
    entries,
    max_entries);
  if (entry_count <= 0) {
    best.reject_debug = hitscan_aim_make_reject_debug(target, aimbot_reject_reason::no_hitbox);
    return best;
  }

  for (int entry_index = 0; entry_index < entry_count; ++entry_index) {
    const hitscan_hitbox_entry& entry = entries[entry_index];
    studio_box* hitbox = hitbox_set->hitbox(entry.studio_hitbox);
    if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= 128) {
      continue;
    }

    constexpr int max_local_points = 10;
    Vec3 local_points[max_local_points]{};
    const bool use_multipoint = entry.hitbox == priority_hitbox &&
      entry.hitbox != aim_hitbox_head &&
      settings.multipoint_scale > 0.0f;
    const int point_count = aimbot_build_local_hitbox_points(
      *hitbox,
      bone_to_world[hitbox->bone],
      shoot_pos,
      local_points,
      max_local_points,
      use_multipoint);

    for (int point_index = 0; point_index < point_count; ++point_index) {
      const Vec3 position = aimbot_transform_point(local_points[point_index], bone_to_world[hitbox->bone]);
      hitscan_point point = hitscan_aim_make_point(
        localplayer,
        target,
        weapon,
        view_angles,
        entry.hitbox,
        entry.studio_hitbox,
        hitbox->bone,
        entry.priority,
        position);
      if (!point.valid) {
        hitscan_aim_keep_reject(&best, point.reject_debug);
        continue;
      }

      if (!best.valid || point.priority < best.priority || (point.priority == best.priority && point.fov < best.fov)) {
        best = point;
      }
    }
  }

  if (!best.valid && best.reject_debug.reason == aimbot_reject_reason::none) {
    best.reject_debug = hitscan_aim_make_reject_debug(target, aimbot_reject_reason::no_point);
  }

  return best;
}

inline int hitscan_aim_build_entity_points(Entity* entity, Vec3* points, int max_points) {
  int point_count = 0;
  if (entity == nullptr || points == nullptr || max_points <= 0) {
    return point_count;
  }

  const Vec3 origin = entity->get_collision_origin();
  const Vec3 mins = entity->get_collideable_mins() + origin;
  const Vec3 maxs = entity->get_collideable_maxs() + origin;
  if (!aimbot_vec3_is_finite(mins) || !aimbot_vec3_is_finite(maxs)) {
    return point_count;
  }

  const Vec3 center = (mins + maxs) * 0.5f;
  aimbot_add_local_hitbox_point(points, &point_count, max_points, center);

  const float scale = std::max(aimbot_effective_multipoint_scale(), aimbot_effective_bone_size_min_scale());
  if (scale <= 0.0f) {
    return point_count;
  }

  const float subtract = aimbot_effective_bone_size_subtract();
  const Vec3 extent_raw = (maxs - mins) * 0.5f;
  const Vec3 extent{
    std::max(extent_raw.x - subtract, extent_raw.x * aimbot_effective_bone_size_min_scale()),
    std::max(extent_raw.y - subtract, extent_raw.y * aimbot_effective_bone_size_min_scale()),
    std::max(extent_raw.z - subtract, extent_raw.z * aimbot_effective_bone_size_min_scale())
  };
  const Vec3 scaled_extent = extent * scale;
  if (std::fabs(scaled_extent.x) <= 1.0f ||
      std::fabs(scaled_extent.y) <= 1.0f ||
      std::fabs(scaled_extent.z) <= 1.0f) {
    return point_count;
  }

  for (const float x_sign : { -1.0f, 1.0f }) {
    for (const float y_sign : { -1.0f, 1.0f }) {
      for (const float z_sign : { -1.0f, 1.0f }) {
        aimbot_add_local_hitbox_point(
          points,
          &point_count,
          max_points,
          center + Vec3{scaled_extent.x * x_sign, scaled_extent.y * y_sign, scaled_extent.z * z_sign});
      }
    }
  }

  return point_count;
}

inline hitscan_point hitscan_aim_make_entity_point(Player* localplayer,
  Entity* target,
  const Vec3& view_angles,
  int priority,
  const Vec3& position) {
  hitscan_point point{};
  if (localplayer == nullptr || target == nullptr || !aimbot_vec3_is_finite(position)) {
    return point;
  }

  hitscan_trace_result trace{};
  if (!hitscan_aim_trace_point(localplayer, target, position, &trace)) {
    return point;
  }

  const Vec3 start_pos = localplayer->get_shoot_pos();
  if ((trace.entity != nullptr && !hitscan_aim_same_entity(trace.entity, target)) ||
      (trace.entity == nullptr && !hitscan_aim_ray_hits_entity_bounds(target, start_pos, position))) {
    return point;
  }

  point.valid = true;
  point.hitbox = -1;
  point.studio_hitbox = -1;
  point.priority = priority;
  point.position = position;
  point.angles = aimbot_calculate_angles_to_position(start_pos, position);
  point.fov = aimbot_calculate_fov(hitscan_aim_command_angles(localplayer, point.angles), view_angles);
  return point;
}

inline aimbot_candidate hitscan_aim_find_non_player_candidate(Player* localplayer,
  Weapon* weapon,
  Entity* entity,
  const Vec3& view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr ||
      weapon == nullptr ||
      entity == nullptr ||
      aimbot_is_projectile_weapon(weapon) ||
      aimbot_is_melee_weapon(weapon) ||
      aimbot_should_skip_non_player_target(localplayer, entity)) {
    return candidate;
  }

  constexpr int max_points = 9;
  Vec3 points[max_points]{};
  const int point_count = hitscan_aim_build_entity_points(entity, points, max_points);
  hitscan_point best{};
  for (int point_index = 0; point_index < point_count; ++point_index) {
    hitscan_point point = hitscan_aim_make_entity_point(
      localplayer,
      entity,
      view_angles,
      point_index,
      points[point_index]);
    if (!point.valid) {
      continue;
    }

    if (!best.valid || point.priority < best.priority || (point.priority == best.priority && point.fov < best.fov)) {
      best = point;
    }
  }

  if (!best.valid) {
    return candidate;
  }

  candidate.entity = entity;
  candidate.aim_position = best.position;
  candidate.aim_angles = best.angles;
  candidate.fov = best.fov;
  candidate.distance = distance_3d(localplayer->get_origin(), entity->get_origin());
  candidate.health = aimbot_entity_health(entity);
  candidate.command_angles = hitscan_aim_command_angles(localplayer, best.angles);
  candidate.visible = true;
  return candidate;
}

inline aimbot_candidate hitscan_aim_make_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const hitscan_point& point,
  const Vec3& view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr || weapon == nullptr || player == nullptr || !point.valid) {
    candidate.player = player;
    candidate.reject_debug = point.reject_debug;
    if (candidate.reject_debug.reason == aimbot_reject_reason::none) {
      candidate.reject_debug = hitscan_aim_make_reject_debug(player, aimbot_reject_reason::no_point);
    }
    return candidate;
  }

  candidate.entity = player;
  candidate.player = player;
  candidate.preferred = aimbot::has_preference(player);
  candidate.bone = point.bone;
  candidate.hitbox = point.hitbox;
  candidate.studio_hitbox = point.studio_hitbox;
  candidate.aim_position = point.position;
  candidate.aim_angles = point.angles;
  candidate.fov = point.fov;
  candidate.distance = distance_3d(localplayer->get_origin(), player->get_origin());
  candidate.health = player->get_health();
  candidate.simulation_time = player->get_simulation_time();
  candidate.tick_count = 0;
  candidate.command_angles = hitscan_aim_command_angles(localplayer, point.angles);
  candidate.visible = true;
  return candidate;
}

inline aimbot_candidate hitscan_aim_find_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& view_angles) {
  return hitscan_aim_make_candidate(
    localplayer,
    weapon,
    player,
    hitscan_aim_find_point(localplayer, weapon, player, view_angles),
    view_angles);
}

inline aimbot_candidate hitscan_aim_find_occluded_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& view_angles) {
  return hitscan_aim_find_candidate(localplayer, weapon, player, view_angles);
}

inline bool hitscan_aim_trace_backtrack_candidate(const aimbot_candidate& candidate,
  const Vec3& start_pos,
  const Vec3& end_pos) {
  if (!candidate.backtrack || !candidate.visible) {
    return false;
  }

  if (!hitscan_aim_world_clear(start_pos, candidate.aim_position)) {
    return false;
  }

  if (candidate.hitbox < 0) {
    return aimbot_segment_intersects_aabb(start_pos, end_pos, candidate.backtrack_mins, candidate.backtrack_maxs);
  }

  if (candidate.backtrack_hitbox_valid) {
    const Vec3 local_start = aimbot_inverse_transform_point(start_pos, candidate.backtrack_bone);
    const Vec3 local_end = aimbot_inverse_transform_point(end_pos, candidate.backtrack_bone);
    constexpr float expansion = 1.25f;
    const Vec3 mins = candidate.backtrack_hitbox_mins - Vec3{expansion, expansion, expansion};
    const Vec3 maxs = candidate.backtrack_hitbox_maxs + Vec3{expansion, expansion, expansion};
    if (!aimbot_segment_intersects_aabb(local_start, local_end, mins, maxs)) {
      return false;
    }
    return aimbot_segment_intersects_aabb(start_pos, end_pos, candidate.backtrack_mins, candidate.backtrack_maxs);
  }

  float horizontal_radius = 5.0f;
  float vertical_radius = 6.0f;
  switch (candidate.hitbox) {
  case aim_hitbox_head:
    horizontal_radius = 5.0f;
    vertical_radius = 4.75f;
    break;
  case aim_hitbox_pelvis:
  case aim_hitbox_spine_0:
    horizontal_radius = 6.0f;
    vertical_radius = 5.0f;
    break;
  case aim_hitbox_left_thigh:
  case aim_hitbox_left_calf:
  case aim_hitbox_left_foot:
  case aim_hitbox_right_thigh:
  case aim_hitbox_right_calf:
  case aim_hitbox_right_foot:
    horizontal_radius = 4.0f;
    vertical_radius = 5.0f;
    break;
  default:
    break;
  }

  const Vec3 mins = candidate.aim_position - Vec3{horizontal_radius, horizontal_radius, vertical_radius};
  const Vec3 maxs = candidate.aim_position + Vec3{horizontal_radius, horizontal_radius, vertical_radius};
  return aimbot_segment_intersects_aabb(start_pos, end_pos, mins, maxs);
}

inline bool hitscan_aim_trace_candidate(Player* localplayer,
  const aimbot_candidate& candidate,
  const Vec3& command_view_angles,
  const Vec3& spread_offset = {},
  bool use_spread = false,
  hitscan_trace_result* result = nullptr) {
  if (result != nullptr) {
    *result = {};
  }

  if (localplayer == nullptr ||
      candidate.entity == nullptr ||
      engine_trace == nullptr ||
      !aimbot_vec3_is_finite(candidate.aim_position)) {
    return false;
  }

  Vec3 start_pos = localplayer->get_shoot_pos();
  const Vec3 bullet_angles = hitscan_aim_bullet_angles(localplayer, command_view_angles);
  Vec3 forward{};
  Vec3 right{};
  Vec3 up{};
  angle_vectors(bullet_angles, &forward, &right, &up);
  if (!aimbot_vec3_is_finite(forward)) {
    return false;
  }

  if (use_spread) {
    forward = local_prediction_normalize(forward + (right * spread_offset.x) + (up * spread_offset.y));
    if (!aimbot_vec3_is_finite(forward)) {
      return false;
    }
  }

  const float trace_distance = distance_3d(start_pos, candidate.aim_position);
  if (trace_distance <= 0.001f) {
    return false;
  }
  const float trace_length = std::max(trace_distance + 64.0f, 128.0f);
  Vec3 end_pos = start_pos + (forward * trace_length);
  hitscan_trace_result trace = hitscan_aim_trace_line(localplayer, start_pos, end_pos, candidate.entity);
  if (result != nullptr) {
    *result = trace;
  }

  if (hitscan_aim_same_entity(trace.entity, candidate.entity) && hitscan_aim_accepts_trace_hitbox(candidate, trace.hitbox)) {
    if (result != nullptr) {
      result->hit = true;
    }
    return true;
  }

  if (trace.entity != nullptr && !hitscan_aim_same_entity(trace.entity, candidate.entity)) {
    return false;
  }

  if (hitscan_aim_trace_backtrack_candidate(candidate, start_pos, end_pos)) {
    if (result != nullptr) {
      result->hit = true;
      result->entity = candidate.entity;
      result->hitbox = candidate.hitbox;
    }
    return true;
  }

  if (candidate.player != nullptr && !candidate.backtrack && candidate.hitbox == aim_hitbox_head) {
    return false;
  }

  if (hitscan_aim_trace_geometry(candidate, start_pos, end_pos)) {
    if (result != nullptr) {
      result->hit = true;
      result->entity = candidate.entity;
      result->hitbox = candidate.hitbox;
    }
    return true;
  }

  return false;
}

inline bool hitscan_aim_headshot_ready(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  return aimbot_wait_for_headshot_ready(localplayer, weapon, candidate);
}

inline bool hitscan_aim_charge_ready(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  return aimbot_wait_for_charge_ready(localplayer, weapon, candidate);
}

#endif
