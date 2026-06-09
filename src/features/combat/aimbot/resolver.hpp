#ifndef AIMBOT_RESOLVER_HPP
#define AIMBOT_RESOLVER_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstring>

#include "aim_utils.hpp"

#include "core/entity_cache.hpp"
#include "core/shared/sigs.hpp"
#include "games/tf2/sdk/netvars.hpp"
#include "libsigscan/libsigscan.h"

namespace resolver
{

constexpr int max_entities = 65;
constexpr int max_records = 16;
constexpr int max_yaw_candidates = 24;
constexpr int max_pitch_candidates = 5;
constexpr int max_pose_parameters = 24;

constexpr std::uintptr_t anim_state_pose_init_offset = 68;
constexpr std::uintptr_t anim_state_aim_yaw_index_offset = 80;
constexpr std::uintptr_t anim_state_aim_pitch_index_offset = 84;
constexpr std::uintptr_t anim_state_move_yaw_index_offset = 92;
constexpr std::uintptr_t anim_state_gait_yaw_offset = 100;
constexpr std::uintptr_t anim_state_eye_yaw_offset = 140;
constexpr std::uintptr_t anim_state_eye_pitch_offset = 144;

//   mov dword ptr [rdi+0xBA0], 0xFF7FFFFF        ; m_flLastBoneSetupTime = -FLT_MAX
//   mov qword ptr [rdi+0x820], g_iModelBoneCounter - 1
constexpr int cached_bone_last_setup_time_delta = -0x30;
constexpr int cached_bone_frame_counter_delta = -0x3B0;

struct anim_state_snapshot {
  bool valid = false;
  float eye_yaw = 0.0f;
  float eye_pitch = 0.0f;
  float raw_eye_pitch = 0.0f;
  float gait_yaw = 0.0f;
  float velocity_yaw = 0.0f;
  float speed_2d = 0.0f;
  bool moving = false;
};

struct resolver_record {
  bool valid = false;
  float sim_time = 0.0f;
  Vec3 origin{};
  Vec3 velocity{};
  float eye_yaw = 0.0f;
  float gait_yaw = 0.0f;
  float velocity_yaw = 0.0f;
  bool moving = false;
};

struct player_history {
  int ent_index = 0;
  int record_count = 0;
  std::array<resolver_record, max_records> records{};
};

struct yaw_candidate {
  bool valid = false;
  float yaw = 0.0f;
  float penalty = 0.0f;
};

struct pitch_candidate {
  bool valid = false;
  float pitch = 0.0f;
  float penalty = 0.0f;
};

struct yaw_candidate_list {
  int count = 0;
  std::array<yaw_candidate, max_yaw_candidates> values{};
};

struct pitch_candidate_list {
  int count = 0;
  std::array<pitch_candidate, max_pitch_candidates> values{};
};

enum class resolver_mode {
  unknown,
  moving,
  standing,
  jitter,
  spin,
  fakewalk,
  sideways_fake
};

struct resolver_debug_info {
  bool active = false;
  int yaw_candidates = 0;
  int misses = 0;
  int hits = 0;
  float yaw = 0.0f;
  float pitch = 0.0f;
  resolver_mode mode = resolver_mode::unknown;
};

struct pending_shot {
  bool active = false;
  float time = 0.0f;
  float expire_time = 0.0f;
  float sim_time = 0.0f;
  float yaw = 0.0f;
  float pitch = 0.0f;
  int hitbox = -1;
  bool backtrack = false;
};

struct player_resolver_state {
  int ent_index = 0;
  int brute_yaw_index = 0;
  int brute_pitch_index = 0;
  int misses = 0;
  int hits = 0;
  int yaw_candidates = 0;
  float selected_yaw = 0.0f;
  float selected_pitch = 0.0f;
  resolver_mode mode = resolver_mode::unknown;
  pending_shot shot{};
};

inline std::array<player_history, max_entities> g_history{};
inline std::array<player_resolver_state, max_entities> g_resolver_state{};

[[nodiscard]] inline float normalize_yaw(float yaw)
{
  return std::remainder(yaw, 360.0f);
}

[[nodiscard]] inline float yaw_delta(float left, float right)
{
  return normalize_yaw(left - right);
}

[[nodiscard]] inline float clamp_pitch(float pitch)
{
  return std::clamp(pitch, -89.0f, 89.0f);
}

[[nodiscard]] inline bool finite_angle(float value)
{
  return std::isfinite(value) && value >= -720.0f && value <= 720.0f;
}

[[nodiscard]] inline float vector_yaw(const Vec3& value)
{
  return normalize_yaw(std::atan2(value.y, value.x) * radpi);
}

[[nodiscard]] inline float speed_2d(const Vec3& value)
{
  return std::sqrt((value.x * value.x) + (value.y * value.y));
}

[[nodiscard]] inline int player_anim_state_offset()
{
  static int offset = -1;
  if (offset >= 0) {
    return offset;
  }

  offset = 0;
  const auto* match = reinterpret_cast<const std::uint8_t*>(
    sigscan_module("client.so", sigs::ctf_player_anim_state_store));
  if (match == nullptr) {
    return offset;
  }

  std::int32_t store_offset = 0;
  std::memcpy(&store_offset, match + 13, sizeof(store_offset));
  if (store_offset >= 0x400 && store_offset <= 0x8000) {
    offset = store_offset;
  }

  return offset;
}

[[nodiscard]] inline int pose_parameter_offset()
{
  static const int offset = tf2_netvars::find_offset("DT_BaseAnimating", { "m_flPoseParameter" });
  return offset;
}

[[nodiscard]] inline int lighting_origin_offset()
{
  static const int offset = tf2_netvars::find_offset("DT_BaseAnimating", { "m_hLightingOrigin" });
  return offset;
}

[[nodiscard]] inline void* player_anim_state(Player* player)
{
  const int offset = player_anim_state_offset();
  if (player == nullptr || offset <= 0) {
    return nullptr;
  }

  const auto base = reinterpret_cast<std::uintptr_t>(player);
  return *reinterpret_cast<void**>(base + static_cast<std::uintptr_t>(offset));
}

[[nodiscard]] inline float* player_pose_parameters(Player* player)
{
  const int offset = pose_parameter_offset();
  if (player == nullptr || offset <= 0) {
    return nullptr;
  }

  const auto base = reinterpret_cast<std::uintptr_t>(player);
  return reinterpret_cast<float*>(base + static_cast<std::uintptr_t>(offset));
}

[[nodiscard]] inline bool valid_pose_index(int index)
{
  return index >= 0 && index < max_pose_parameters;
}

[[nodiscard]] inline float angle_to_pose(float angle)
{
  return std::clamp((normalize_yaw(angle) + 180.0f) / 360.0f, 0.0f, 1.0f);
}

[[nodiscard]] inline float pitch_to_pose(float pitch)
{
  return std::clamp((clamp_pitch(pitch) + 90.0f) / 180.0f, 0.0f, 1.0f);
}

//   m_flLastBoneSetupTime       = -FLT_MAX
//   m_iMostRecentModelBoneCounter = g_iModelBoneCounter - 1
inline void force_bone_rebuild(Player* player)
{
  const int origin_offset = lighting_origin_offset();
  if (player == nullptr || origin_offset <= 0) {
    return;
  }

  constexpr int min_required_offset = -cached_bone_frame_counter_delta + 0x10;
  if (origin_offset < min_required_offset) {
    return;
  }

  auto* anchor = reinterpret_cast<std::uint8_t*>(player) + origin_offset;
  *reinterpret_cast<float*>(anchor + cached_bone_last_setup_time_delta) = -FLT_MAX;
  *reinterpret_cast<std::uint64_t*>(anchor + cached_bone_frame_counter_delta) = 0;
}

[[nodiscard]] inline bool read_anim_state_snapshot(Player* player, anim_state_snapshot* snapshot)
{
  if (snapshot == nullptr) {
    return false;
  }

  *snapshot = {};
  if (player == nullptr) {
    return false;
  }

  void* raw_state = player_anim_state(player);
  if (raw_state == nullptr) {
    return false;
  }

  const auto state = reinterpret_cast<std::uintptr_t>(raw_state);
  const auto owner_multi = *reinterpret_cast<void**>(state + 48);
  const auto owner_ctf = *reinterpret_cast<void**>(state + 304);
  if (owner_multi != player && owner_ctf != player) {
    return false;
  }

  Vec3 eye_angles = player->get_eye_angles();
  float eye_yaw = *reinterpret_cast<float*>(state + anim_state_eye_yaw_offset);
  const float ctf_eye_yaw = *reinterpret_cast<float*>(state + 60);
  if (!finite_angle(eye_yaw) && finite_angle(ctf_eye_yaw)) {
    eye_yaw = ctf_eye_yaw;
  }
  if (!finite_angle(eye_yaw)) {
    eye_yaw = eye_angles.y;
  }

  float eye_pitch = *reinterpret_cast<float*>(state + anim_state_eye_pitch_offset);
  if (!finite_angle(eye_pitch)) {
    eye_pitch = eye_angles.x;
  }

  float gait_yaw = *reinterpret_cast<float*>(state + anim_state_gait_yaw_offset);
  if (!finite_angle(gait_yaw)) {
    gait_yaw = eye_yaw;
  }

  const Vec3 velocity = player->get_velocity();
  const float movement_speed = speed_2d(velocity);

  snapshot->valid = true;
  snapshot->eye_yaw = normalize_yaw(eye_yaw);
  snapshot->eye_pitch = clamp_pitch(eye_pitch);
  snapshot->raw_eye_pitch = finite_angle(eye_angles.x) ? eye_angles.x : eye_pitch;
  snapshot->gait_yaw = normalize_yaw(gait_yaw);
  snapshot->velocity_yaw = movement_speed > 1.0f ? vector_yaw(velocity) : snapshot->gait_yaw;
  snapshot->speed_2d = movement_speed;
  snapshot->moving = movement_speed > 18.0f;
  return true;
}

[[nodiscard]] inline player_history* history_for_player(Player* player)
{
  if (player == nullptr) {
    return nullptr;
  }

  const int ent_index = player->get_index();
  if (ent_index <= 0 || ent_index >= max_entities) {
    return nullptr;
  }

  return &g_history[ent_index];
}

[[nodiscard]] inline player_resolver_state* state_for_player(Player* player)
{
  if (player == nullptr) {
    return nullptr;
  }

  const int ent_index = player->get_index();
  if (ent_index <= 0 || ent_index >= max_entities) {
    return nullptr;
  }

  player_resolver_state& state = g_resolver_state[ent_index];
  state.ent_index = ent_index;
  return &state;
}

[[nodiscard]] inline const player_resolver_state* state_for_player_const(Player* player)
{
  if (player == nullptr) {
    return nullptr;
  }

  const int ent_index = player->get_index();
  if (ent_index <= 0 || ent_index >= max_entities) {
    return nullptr;
  }

  return &g_resolver_state[ent_index];
}

inline void record_player(Player* player)
{
  if (!config.aimbot.resolver || player == nullptr || player->is_dormant() || !player->is_alive()) {
    return;
  }

  player_history* history = history_for_player(player);
  if (history == nullptr) {
    return;
  }

  anim_state_snapshot snapshot{};
  if (!read_anim_state_snapshot(player, &snapshot)) {
    return;
  }

  resolver_record record{};
  record.valid = true;
  record.sim_time = player->get_simulation_time();
  record.origin = player->get_origin();
  record.velocity = player->get_velocity();
  record.eye_yaw = snapshot.eye_yaw;
  record.gait_yaw = snapshot.gait_yaw;
  record.velocity_yaw = snapshot.velocity_yaw;
  record.moving = snapshot.moving;

  if (!std::isfinite(record.sim_time) || record.sim_time <= 0.0f) {
    return;
  }

  history->ent_index = player->get_index();
  if (history->record_count > 0) {
    const resolver_record& last = history->records[0];
    if (last.valid &&
        std::fabs(last.sim_time - record.sim_time) <= 0.0001f &&
        std::fabs(yaw_delta(last.eye_yaw, record.eye_yaw)) <= 0.01f) {
      return;
    }
  }

  const int shift_count = std::min(history->record_count, max_records - 1);
  for (int index = shift_count; index > 0; --index) {
    history->records[index] = history->records[index - 1];
  }

  history->records[0] = record;
  history->record_count = std::min(history->record_count + 1, max_records);
}

inline void clear()
{
  g_history = {};
  g_resolver_state = {};
}

inline void add_yaw(yaw_candidate_list* list, float yaw, float penalty)
{
  if (list == nullptr || list->count >= max_yaw_candidates || !finite_angle(yaw)) {
    return;
  }

  const float normalized = normalize_yaw(yaw);
  for (int index = 0; index < list->count; ++index) {
    if (std::fabs(yaw_delta(list->values[index].yaw, normalized)) < 2.0f) {
      list->values[index].penalty = std::min(list->values[index].penalty, penalty);
      return;
    }
  }

  yaw_candidate& candidate = list->values[list->count];
  candidate.valid = true;
  candidate.yaw = normalized;
  candidate.penalty = penalty;
  ++list->count;
}

inline void add_pitch(pitch_candidate_list* list, float pitch, float penalty)
{
  if (list == nullptr || list->count >= max_pitch_candidates || !finite_angle(pitch)) {
    return;
  }

  const float clamped = clamp_pitch(pitch);
  for (int index = 0; index < list->count; ++index) {
    if (std::fabs(list->values[index].pitch - clamped) < 1.0f) {
      list->values[index].penalty = std::min(list->values[index].penalty, penalty);
      return;
    }
  }

  pitch_candidate& candidate = list->values[list->count];
  candidate.valid = true;
  candidate.pitch = clamped;
  candidate.penalty = penalty;
  ++list->count;
}

[[nodiscard]] inline float yaw_to_local(Player* localplayer, Player* player)
{
  if (localplayer == nullptr || player == nullptr) {
    return 0.0f;
  }

  return aimbot_calculate_angles_to_position(player->get_origin(), localplayer->get_origin()).y;
}

[[nodiscard]] inline resolver_mode detect_mode(Player* player, const anim_state_snapshot& snapshot)
{
  const player_history* history = history_for_player(player);
  const bool fakewalk = snapshot.speed_2d > 2.0f && snapshot.speed_2d <= 18.0f;

  if (history != nullptr && history->record_count >= 3) {
    const resolver_record& newest = history->records[0];
    const resolver_record& previous = history->records[1];
    const resolver_record& older = history->records[2];
    if (newest.valid && previous.valid && older.valid) {
      const float step_a = yaw_delta(newest.eye_yaw, previous.eye_yaw);
      const float step_b = yaw_delta(previous.eye_yaw, older.eye_yaw);
      const float abs_step_a = std::fabs(step_a);
      const float abs_step_b = std::fabs(step_b);
      if (abs_step_a > 140.0f && abs_step_b > 140.0f && (step_a > 0.0f) != (step_b > 0.0f)) {
        return resolver_mode::sideways_fake;
      }

      if (std::fabs(step_a) > 25.0f && std::fabs(step_b) > 25.0f) {
        if ((step_a > 0.0f) != (step_b > 0.0f)) {
          return resolver_mode::jitter;
        }

        if (std::fabs(std::fabs(step_a) - std::fabs(step_b)) < 35.0f) {
          return resolver_mode::spin;
        }
      }
    }
  }

  if (fakewalk) {
    return resolver_mode::fakewalk;
  }

  return snapshot.moving ? resolver_mode::moving : resolver_mode::standing;
}

inline void add_mode_yaws(Player* localplayer,
  Player* player,
  yaw_candidate_list* list,
  const anim_state_snapshot& snapshot,
  resolver_mode mode,
  const player_resolver_state* state)
{
  if (list == nullptr) {
    return;
  }

  const float base_yaw = snapshot.valid ? snapshot.eye_yaw : player->get_eye_angles().y;
  const float gait_yaw = snapshot.valid ? snapshot.gait_yaw : base_yaw;
  const float local_yaw = yaw_to_local(localplayer, player);
  const float movement_yaw = snapshot.moving ? snapshot.velocity_yaw : gait_yaw;
  const int brute_index = state != nullptr ? state->brute_yaw_index : 0;
  constexpr std::array<float, 9> brute_offsets = {
    0.0f,
    180.0f,
    -58.0f,
    58.0f,
    -90.0f,
    90.0f,
    -120.0f,
    120.0f,
    -180.0f
  };

  const float brute_offset = brute_offsets[static_cast<std::size_t>(std::abs(brute_index) % static_cast<int>(brute_offsets.size()))];
  const float brute_base = mode == resolver_mode::moving ? movement_yaw : base_yaw;
  add_yaw(list, brute_base + brute_offset, -4.0f);

  switch (mode) {
  case resolver_mode::moving:
    add_yaw(list, snapshot.velocity_yaw, -3.0f);
    add_yaw(list, gait_yaw, -2.0f);
    break;
  case resolver_mode::fakewalk:
    add_yaw(list, gait_yaw, -3.0f);
    add_yaw(list, local_yaw + 180.0f, -1.5f);
    add_yaw(list, base_yaw + 180.0f, -1.0f);
    break;
  case resolver_mode::jitter:
    if (const player_history* history = history_for_player(player); history != nullptr && history->record_count >= 2) {
      const float jitter_delta = std::clamp(std::fabs(yaw_delta(history->records[0].eye_yaw, history->records[1].eye_yaw)), 35.0f, 180.0f);
      add_yaw(list, base_yaw + jitter_delta, -3.0f);
      add_yaw(list, base_yaw - jitter_delta, -3.0f);
      add_yaw(list, history->records[1].eye_yaw, -2.0f);
    }
    break;
  case resolver_mode::spin:
    if (const player_history* history = history_for_player(player); history != nullptr && history->record_count >= 2) {
      const float spin_step = yaw_delta(history->records[0].eye_yaw, history->records[1].eye_yaw);
      add_yaw(list, base_yaw + spin_step, -2.5f);
      add_yaw(list, base_yaw + (spin_step * 2.0f), -2.0f);
    }
    break;
  case resolver_mode::sideways_fake:
    add_yaw(list, base_yaw + 90.0f, -5.0f);
    add_yaw(list, base_yaw - 90.0f, -5.0f);
    add_yaw(list, gait_yaw, -2.5f);
    if (const player_history* history = history_for_player(player); history != nullptr && history->record_count >= 2) {
      add_yaw(list, history->records[1].eye_yaw + 90.0f, -2.0f);
      add_yaw(list, history->records[1].eye_yaw - 90.0f, -2.0f);
    }
    break;
  case resolver_mode::standing:
  case resolver_mode::unknown:
    add_yaw(list, base_yaw, -3.0f);
    add_yaw(list, base_yaw + 180.0f, -1.5f);
    add_yaw(list, gait_yaw, -1.0f);
    break;
  }
}

inline void add_history_yaws(Player* player, yaw_candidate_list* list)
{
  const player_history* history = history_for_player(player);
  if (history == nullptr || list == nullptr || history->record_count <= 0) {
    return;
  }

  const resolver_record& latest = history->records[0];
  if (latest.valid) {
    add_yaw(list, latest.eye_yaw, 2.0f);
    add_yaw(list, latest.gait_yaw, 3.0f);
    if (latest.moving) {
      add_yaw(list, latest.velocity_yaw, 1.0f);
    }
  }

  for (int index = 1; index < history->record_count; ++index) {
    const resolver_record& record = history->records[index];
    if (!record.valid) {
      continue;
    }

    if (record.moving) {
      add_yaw(list, record.velocity_yaw, 4.0f);
      add_yaw(list, record.gait_yaw, 5.0f);
      break;
    }
  }

  if (history->record_count >= 2 && history->records[0].valid && history->records[1].valid) {
    const float yaw_step = yaw_delta(history->records[0].eye_yaw, history->records[1].eye_yaw);
    if (std::fabs(yaw_step) <= 90.0f) {
      add_yaw(list, history->records[0].eye_yaw + yaw_step, 6.0f);
      add_yaw(list, history->records[0].gait_yaw + yaw_step, 7.0f);
    }
  }
}

[[nodiscard]] inline yaw_candidate_list build_yaw_candidates(Player* localplayer,
  Player* player,
  const anim_state_snapshot& snapshot,
  resolver_mode mode,
  const player_resolver_state* state)
{
  yaw_candidate_list list{};
  const Vec3 eye_angles = player != nullptr ? player->get_eye_angles() : Vec3{};
  const float eye_yaw = finite_angle(eye_angles.y) ? eye_angles.y : snapshot.eye_yaw;
  const float base_yaw = snapshot.valid ? snapshot.eye_yaw : eye_yaw;
  const float gait_yaw = snapshot.valid ? snapshot.gait_yaw : base_yaw;
  const float local_yaw = yaw_to_local(localplayer, player);

  add_mode_yaws(localplayer, player, &list, snapshot, mode, state);

  add_yaw(&list, base_yaw, 0.0f);
  add_yaw(&list, gait_yaw, snapshot.moving ? 0.5f : 2.5f);
  if (mode == resolver_mode::sideways_fake) {
    add_yaw(&list, base_yaw + 90.0f, -5.0f);
    add_yaw(&list, base_yaw - 90.0f, -5.0f);
    add_yaw(&list, gait_yaw + 90.0f, -1.0f);
    add_yaw(&list, gait_yaw - 90.0f, -1.0f);
  }
  if (snapshot.valid && snapshot.moving) {
    add_yaw(&list, snapshot.velocity_yaw, 0.25f);
  }

  add_history_yaws(player, &list);

  add_yaw(&list, local_yaw, 8.0f);
  add_yaw(&list, local_yaw + 180.0f, 8.5f);
  add_yaw(&list, local_yaw + 90.0f, 9.0f);
  add_yaw(&list, local_yaw - 90.0f, 9.0f);

  constexpr std::array<float, 8> desync_offsets = {
    -58.0f,
    58.0f,
    -90.0f,
    90.0f,
    -120.0f,
    120.0f,
    180.0f,
    -180.0f
  };

  for (const float offset : desync_offsets) {
    add_yaw(&list, base_yaw + offset, 10.0f + (std::fabs(offset) * 0.01f));
  }

  for (const float offset : desync_offsets) {
    add_yaw(&list, gait_yaw + offset, 11.0f + (std::fabs(offset) * 0.01f));
  }

  return list;
}

[[nodiscard]] inline pitch_candidate_list build_pitch_candidates(Player* player,
  const anim_state_snapshot& snapshot,
  const player_resolver_state* state)
{
  pitch_candidate_list list{};
  const Vec3 eye_angles = player != nullptr ? player->get_eye_angles() : Vec3{};
  const float raw_pitch = finite_angle(eye_angles.x) ? eye_angles.x : snapshot.raw_eye_pitch;
  const float base_pitch = snapshot.valid ? snapshot.eye_pitch : clamp_pitch(raw_pitch);
  constexpr std::array<float, 5> brute_pitches = {
    0.0f,
    -89.0f,
    89.0f,
    -45.0f,
    45.0f
  };
  const int brute_index = state != nullptr ? state->brute_pitch_index : 0;

  add_pitch(&list, base_pitch, 0.0f);
  add_pitch(&list, brute_pitches[static_cast<std::size_t>(std::abs(brute_index) % static_cast<int>(brute_pitches.size()))], -1.0f);

  for (Entity* entity : entity_cache_entities(class_id::SNIPER_DOT)) {
    if (entity == nullptr || entity->is_dormant() || entity->get_owner_entity() != player) {
      continue;
    }

    const Vec3 dot_origin = entity->get_origin();
    const Vec3 eye_origin = player->get_origin() + player->get_view_offset();
    if (aimbot_vec3_is_finite(dot_origin) && aimbot_vec3_is_finite(eye_origin)) {
      add_pitch(&list, aimbot_calculate_angles_to_position(eye_origin, dot_origin).x, -3.0f);
      break;
    }
  }

  if (finite_angle(raw_pitch) && std::fabs(raw_pitch) >= 89.0f) {
    add_pitch(&list, -base_pitch, 1.0f);
    add_pitch(&list, raw_pitch > 0.0f ? -89.0f : 89.0f, 1.5f);
    add_pitch(&list, 0.0f, 2.0f);
  }

  if (list.count <= 0) {
    add_pitch(&list, 0.0f, 4.0f);
  }

  return list;
}

[[nodiscard]] inline int candidate_exposure_count(Player* localplayer,
  Player* player,
  std::uint32_t hitbox_mask,
  bool require_visibility,
  unsigned int trace_mask)
{
  if (!require_visibility || localplayer == nullptr || player == nullptr || model_info == nullptr) {
    return 0;
  }

  const model_t* model = player->get_model();
  studio_hdr* hdr = model != nullptr ? model_info->get_studio_model(model) : nullptr;
  studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(player->get_hitbox_set()) : nullptr;
  if (hitbox_set == nullptr) {
    return 0;
  }

  matrix_3x4 bone_to_world[128]{};
  if (!player->setup_bones(bone_to_world, 128, 0x100, player->get_simulation_time())) {
    return 0;
  }

  int exposed = 0;
  for (int hitbox_id = aim_hitbox_head; hitbox_id <= aim_hitbox_right_foot; ++hitbox_id) {
    if (!aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask) || hitbox_id >= hitbox_set->num_hitboxes) {
      continue;
    }

    studio_box* hitbox = hitbox_set->hitbox(hitbox_id);
    if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= 128) {
      continue;
    }

    const Vec3 center = aimbot_transform_point((hitbox->bbmin + hitbox->bbmax) * 0.5f, bone_to_world[hitbox->bone]);
    if (aimbot_vec3_is_finite(center) && aimbot_trace_visible_to_position(localplayer, player, center, trace_mask)) {
      ++exposed;
    }
  }

  return exposed;
}

inline void sort_yaw_candidates(yaw_candidate_list* list)
{
  if (list == nullptr || list->count <= 1) {
    return;
  }

  std::sort(list->values.begin(), list->values.begin() + list->count,
    [](const yaw_candidate& left, const yaw_candidate& right) {
      return left.penalty < right.penalty;
    });
}

inline void sort_pitch_candidates(pitch_candidate_list* list)
{
  if (list == nullptr || list->count <= 1) {
    return;
  }

  std::sort(list->values.begin(), list->values.begin() + list->count,
    [](const pitch_candidate& left, const pitch_candidate& right) {
      return left.penalty < right.penalty;
    });
}

struct scoped_bone_cache_bypass {
  bool previous = false;
  bool changed = false;

  scoped_bone_cache_bypass()
  {
    previous = config.misc.exploits.setup_bones_optimization;
    changed = previous;
    if (changed) {
      config.misc.exploits.setup_bones_optimization = false;
    }
  }

  ~scoped_bone_cache_bypass()
  {
    if (changed) {
      config.misc.exploits.setup_bones_optimization = previous;
    }
  }
};

struct scoped_resolved_player_state {
  struct saved_pose_parameter {
    int index = -1;
    float value = 0.0f;
    bool valid = false;
  };

  Player* player = nullptr;
  void* raw_state = nullptr;
  float* pose_parameters = nullptr;
  Vec3 original{};
  float original_gait_yaw = 0.0f;
  float original_eye_yaw = 0.0f;
  float original_eye_pitch = 0.0f;
  bool state_active = false;
  bool active = false;
  int saved_pose_count = 0;
  std::array<saved_pose_parameter, 4> saved_poses{};

  explicit scoped_resolved_player_state(Player* target_player)
    : player(target_player)
  {
    if (player != nullptr) {
      original = player->get_eye_angles();
      raw_state = player_anim_state(player);
      pose_parameters = player_pose_parameters(player);
      if (raw_state != nullptr) {
        const auto state = reinterpret_cast<std::uintptr_t>(raw_state);
        original_gait_yaw = *reinterpret_cast<float*>(state + anim_state_gait_yaw_offset);
        original_eye_yaw = *reinterpret_cast<float*>(state + anim_state_eye_yaw_offset);
        original_eye_pitch = *reinterpret_cast<float*>(state + anim_state_eye_pitch_offset);
        state_active = true;
      }
      active = true;
    }
  }

  ~scoped_resolved_player_state()
  {
    if (active && player != nullptr) {
      player->set_eye_angles(original);
      restore_pose_parameters();
      if (state_active && raw_state != nullptr) {
        const auto state = reinterpret_cast<std::uintptr_t>(raw_state);
        *reinterpret_cast<float*>(state + anim_state_gait_yaw_offset) = original_gait_yaw;
        *reinterpret_cast<float*>(state + anim_state_eye_yaw_offset) = original_eye_yaw;
        *reinterpret_cast<float*>(state + anim_state_eye_pitch_offset) = original_eye_pitch;
      }
      force_bone_rebuild(player);
    }
  }

  void save_pose_parameter(int index)
  {
    if (pose_parameters == nullptr || !valid_pose_index(index)) {
      return;
    }

    for (int saved_index = 0; saved_index < saved_pose_count; ++saved_index) {
      if (saved_poses[saved_index].valid && saved_poses[saved_index].index == index) {
        return;
      }
    }

    if (saved_pose_count >= static_cast<int>(saved_poses.size())) {
      return;
    }

    saved_pose_parameter& saved = saved_poses[saved_pose_count];
    saved.index = index;
    saved.value = pose_parameters[index];
    saved.valid = true;
    ++saved_pose_count;
  }

  void restore_pose_parameters()
  {
    if (pose_parameters == nullptr) {
      return;
    }

    for (int index = 0; index < saved_pose_count; ++index) {
      const saved_pose_parameter& saved = saved_poses[index];
      if (saved.valid && valid_pose_index(saved.index)) {
        pose_parameters[saved.index] = saved.value;
      }
    }
  }

  void write_pose_parameter(int index, float value)
  {
    if (pose_parameters == nullptr || !valid_pose_index(index)) {
      return;
    }

    save_pose_parameter(index);
    pose_parameters[index] = std::clamp(value, 0.0f, 1.0f);
  }

  void apply(const anim_state_snapshot& snapshot, float pitch, float yaw)
  {
    if (!active || player == nullptr) {
      return;
    }

    const float clamped_pitch = clamp_pitch(pitch);
    const float normalized_yaw = normalize_yaw(yaw);
    player->set_eye_angles(Vec3{ clamped_pitch, normalized_yaw, 0.0f });

    if (state_active && raw_state != nullptr) {
      const auto state = reinterpret_cast<std::uintptr_t>(raw_state);
      const float gait_yaw = snapshot.moving ? snapshot.velocity_yaw : snapshot.gait_yaw;
      const float body_yaw = yaw_delta(gait_yaw, normalized_yaw);

      *reinterpret_cast<float*>(state + anim_state_gait_yaw_offset) = normalize_yaw(gait_yaw);
      *reinterpret_cast<float*>(state + anim_state_eye_yaw_offset) = normalized_yaw;
      *reinterpret_cast<float*>(state + anim_state_eye_pitch_offset) = clamped_pitch;

      const bool pose_initialized = *reinterpret_cast<bool*>(state + anim_state_pose_init_offset);
      if (pose_initialized) {
        const int aim_yaw_index = *reinterpret_cast<int*>(state + anim_state_aim_yaw_index_offset);
        const int aim_pitch_index = *reinterpret_cast<int*>(state + anim_state_aim_pitch_index_offset);
        const int move_yaw_index = *reinterpret_cast<int*>(state + anim_state_move_yaw_index_offset);

        write_pose_parameter(aim_yaw_index, angle_to_pose(body_yaw));
        write_pose_parameter(aim_pitch_index, pitch_to_pose(clamped_pitch));
        write_pose_parameter(move_yaw_index, angle_to_pose(body_yaw));
      }
    }

    force_bone_rebuild(player);
  }
};

[[nodiscard]] inline float resolver_point_score(const aimbot_point& point, float angle_penalty, int exposed_hitboxes)
{
  return (static_cast<float>(point.priority) * 4096.0f) + point.fov + angle_penalty - (static_cast<float>(exposed_hitboxes) * 3.0f);
}

[[nodiscard]] inline const char* mode_name(resolver_mode mode)
{
  switch (mode) {
  case resolver_mode::moving:
    return "moving";
  case resolver_mode::standing:
    return "standing";
  case resolver_mode::jitter:
    return "jitter";
  case resolver_mode::spin:
    return "spin";
  case resolver_mode::fakewalk:
    return "fakewalk";
  case resolver_mode::sideways_fake:
    return "sideways";
  case resolver_mode::unknown:
    break;
  }

  return "unknown";
}

[[nodiscard]] inline resolver_debug_info debug_for_player(Player* player)
{
  resolver_debug_info info{};
  const player_resolver_state* state = state_for_player_const(player);
  if (state == nullptr || state->ent_index <= 0) {
    return info;
  }

  info.active = true;
  info.yaw_candidates = state->yaw_candidates;
  info.misses = state->misses;
  info.hits = state->hits;
  info.yaw = state->selected_yaw;
  info.pitch = state->selected_pitch;
  info.mode = state->mode;
  return info;
}

inline void update_pending_shots()
{
  if (global_vars == nullptr) {
    return;
  }

  for (player_resolver_state& state : g_resolver_state) {
    if (!state.shot.active || global_vars->curtime < state.shot.expire_time) {
      continue;
    }

    state.shot.active = false;
    state.misses = std::min(state.misses + 1, 64);
    state.brute_yaw_index = (state.brute_yaw_index + 1) % 9;
    if ((state.misses % 2) == 0) {
      state.brute_pitch_index = (state.brute_pitch_index + 1) % max_pitch_candidates;
    }
  }
}

inline void note_shot(Player* player, int hitbox, float sim_time, bool backtrack)
{
  if (!config.aimbot.resolver || player == nullptr || global_vars == nullptr) {
    return;
  }

  player_resolver_state* state = state_for_player(player);
  if (state == nullptr) {
    return;
  }

  pending_shot shot{};
  shot.active = true;
  shot.time = global_vars->curtime;
  shot.expire_time = global_vars->curtime + (backtrack ? 0.55f : 0.35f);
  shot.sim_time = sim_time;
  shot.yaw = state->selected_yaw;
  shot.pitch = state->selected_pitch;
  shot.hitbox = hitbox;
  shot.backtrack = backtrack;
  state->shot = shot;
}

inline void note_player_hurt(Player* attacker, Player* victim)
{
  if (!config.aimbot.resolver || attacker == nullptr || victim == nullptr || entity_list == nullptr) {
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || attacker != localplayer) {
    return;
  }

  player_resolver_state* state = state_for_player(victim);
  if (state == nullptr) {
    return;
  }

  state->shot.active = false;
  state->hits = std::min(state->hits + 1, 64);
  state->misses = std::max(state->misses - 1, 0);
}

[[nodiscard]] inline bool setup_record_bones(Player* player, matrix_3x4* bones, int max_bones, float sim_time)
{
  if (!config.aimbot.resolver || player == nullptr || bones == nullptr || max_bones <= 0) {
    return false;
  }

  anim_state_snapshot snapshot{};
  if (!read_anim_state_snapshot(player, &snapshot)) {
    return false;
  }

  player_resolver_state* state = state_for_player(player);
  const resolver_mode mode = detect_mode(player, snapshot);
  if (state != nullptr) {
    state->mode = mode;
  }

  yaw_candidate_list yaw_candidates = build_yaw_candidates(nullptr, player, snapshot, mode, state);
  pitch_candidate_list pitch_candidates = build_pitch_candidates(player, snapshot, state);
  sort_yaw_candidates(&yaw_candidates);
  sort_pitch_candidates(&pitch_candidates);
  if (yaw_candidates.count <= 0 || pitch_candidates.count <= 0) {
    return false;
  }

  scoped_bone_cache_bypass bone_cache_bypass{};
  scoped_resolved_player_state resolved_state(player);
  resolved_state.apply(snapshot, pitch_candidates.values[0].pitch, yaw_candidates.values[0].yaw);
  const bool result = player->setup_bones(bones, max_bones, 0x100, sim_time) != 0;

  if (state != nullptr) {
    state->yaw_candidates = yaw_candidates.count;
    state->selected_yaw = yaw_candidates.values[0].yaw;
    state->selected_pitch = pitch_candidates.values[0].pitch;
  }

  return result;
}

[[nodiscard]] inline aimbot_point find_point(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& bullet_view_angles,
  bool require_visibility,
  unsigned int trace_mask)
{
  if (!config.aimbot.resolver || localplayer == nullptr || weapon == nullptr || player == nullptr) {
    return {};
  }

  anim_state_snapshot snapshot{};
  if (!read_anim_state_snapshot(player, &snapshot)) {
    return {};
  }

  player_resolver_state* state = state_for_player(player);
  const resolver_mode mode = detect_mode(player, snapshot);
  if (state != nullptr) {
    state->mode = mode;
  }

  yaw_candidate_list yaw_candidates = build_yaw_candidates(localplayer, player, snapshot, mode, state);
  pitch_candidate_list pitch_candidates = build_pitch_candidates(player, snapshot, state);
  sort_yaw_candidates(&yaw_candidates);
  sort_pitch_candidates(&pitch_candidates);
  if (yaw_candidates.count <= 0 || pitch_candidates.count <= 0) {
    return {};
  }

  const std::uint32_t configured_mask = config.aimbot.hitscan_hitboxes & aim_hitbox_mask_all;
  const std::uint32_t hitbox_mask = configured_mask;
  const int max_candidates = std::clamp(config.aimbot.resolver_max_yaws, 4, max_yaw_candidates);

  scoped_bone_cache_bypass bone_cache_bypass{};
  aimbot_point best_point{};
  float best_score = FLT_MAX;
  float best_yaw = 0.0f;
  float best_pitch = 0.0f;
  if (state != nullptr) {
    state->yaw_candidates = yaw_candidates.count;
  }

  for (int yaw_index = 0; yaw_index < yaw_candidates.count && yaw_index < max_candidates; ++yaw_index) {
    const yaw_candidate& yaw_candidate_value = yaw_candidates.values[yaw_index];
    if (!yaw_candidate_value.valid) {
      continue;
    }

    for (int pitch_index = 0; pitch_index < pitch_candidates.count; ++pitch_index) {
      const pitch_candidate& pitch_candidate_value = pitch_candidates.values[pitch_index];
      if (!pitch_candidate_value.valid) {
        continue;
      }

      scoped_resolved_player_state resolved_state(player);
      resolved_state.apply(snapshot, pitch_candidate_value.pitch, yaw_candidate_value.yaw);
      aimbot_point point = aimbot_find_best_point(
        localplayer,
        player,
        weapon,
        bullet_view_angles,
        hitbox_mask,
        require_visibility,
        trace_mask);
      if (!point.valid) {
        continue;
      }

      const int exposed_hitboxes = candidate_exposure_count(
        localplayer,
        player,
        hitbox_mask,
        require_visibility,
        trace_mask);
      const float score = resolver_point_score(
        point,
        yaw_candidate_value.penalty + pitch_candidate_value.penalty,
        exposed_hitboxes);
      if (!best_point.valid || score < best_score) {
        best_point = point;
        best_score = score;
        best_yaw = yaw_candidate_value.yaw;
        best_pitch = pitch_candidate_value.pitch;
      }
    }
  }

  if (best_point.valid && state != nullptr) {
    state->selected_yaw = best_yaw;
    state->selected_pitch = best_pitch;
  }

  return best_point;
}

} // namespace resolver

#endif
