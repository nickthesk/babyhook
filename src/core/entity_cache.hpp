/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/entity_cache.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef ENTITY_CACHE_HPP
#define ENTITY_CACHE_HPP

#include <cstdint>
#include <utility>
#include <vector>
#include <unordered_map>

#include "types.hpp"

#include "games/tf2/sdk/entities/entity.hpp"

class Player;

struct entity_cache_player_entry {
  Player* player = nullptr;
  Entity* entity = nullptr;
  int index = 0;
  float simulation_time = 0.0f;
  Vec3 origin{};
  Vec3 velocity{};
  tf_team team = tf_team::UNKNOWN;
  int player_class = 0;
  bool alive = false;
  bool dormant = true;
  bool friendly = false;
  bool ignored = false;
};

struct entity_cache_snapshot {
  std::uint32_t serial = 0;
  std::vector<entity_cache_player_entry> players{};
  std::unordered_map<enum class_id, std::vector<Entity*>> entities{};
};

// These are populated in core/hooks/frame_stage_notify.cpp
inline static std::unordered_map<enum class_id, std::vector<Entity*>> entity_cache;
inline static entity_cache_snapshot g_entity_cache_snapshot;
inline static std::unordered_map<unsigned long, bool> friend_cache;

inline const entity_cache_snapshot& entity_cache_current_snapshot() {
  return g_entity_cache_snapshot;
}

inline const std::vector<entity_cache_player_entry>& entity_cache_players() {
  return g_entity_cache_snapshot.players;
}

inline const std::vector<Entity*>& entity_cache_entities(enum class_id id) {
  static const std::vector<Entity*> empty_entities{};
  const auto found = g_entity_cache_snapshot.entities.find(id);
  return found != g_entity_cache_snapshot.entities.end() ? found->second : empty_entities;
}

inline bool entity_cache_snapshot_contains_player(Player* player) {
  if (player == nullptr) {
    return false;
  }

  for (const entity_cache_player_entry& entry : g_entity_cache_snapshot.players) {
    if (entry.player == player && entry.alive && !entry.dormant) {
      return true;
    }
  }

  return false;
}

inline void entity_cache_clear_snapshot() {
  const std::uint32_t next_serial = g_entity_cache_snapshot.serial + 1;
  g_entity_cache_snapshot = {};
  g_entity_cache_snapshot.serial = next_serial;
}

inline void entity_cache_publish_snapshot(entity_cache_snapshot&& snapshot) {
  snapshot.serial = g_entity_cache_snapshot.serial + 1;
  g_entity_cache_snapshot = std::move(snapshot);
}

struct PickupItem {
  Vec3 location;
  float time;
};
inline static std::vector<PickupItem> pickup_item_cache;

#endif
