/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/setup_bones.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "setup_bones.hpp"

#include <cstdint>
#include <cstring>

#include "features/automation/nographics/nographics.hpp"
#include "features/menu/config.hpp"

#include "games/tf2/sdk/netvars.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/model_info.hpp"

namespace
{

struct cached_bone_data
{
  matrix_3x4* data = nullptr;
  int allocation_count = 0;
  int grow_size = 0;
  int size = 0;
  matrix_3x4* elements = nullptr;
};

auto cached_bone_data_offset() -> int
{
  static int offset = 0;
  static bool initialized = false;

  if (!initialized)
  {
    initialized = true;
    const int lighting_origin_offset = tf2_netvars::find_offset("DT_BaseAnimating", { "m_hLightingOrigin" });
    if (lighting_origin_offset > 0x58)
    {
      offset = lighting_origin_offset - 0x58;
    }
  }

  return offset;
}

auto should_use_cached_bones(Player* player, void* bone_to_world_out) -> bool
{
  if (!config.misc.exploits.setup_bones_optimization ||
      nographics::is_enabled() ||
      player == nullptr ||
      bone_to_world_out == nullptr ||
      entity_list == nullptr)
  {
    return false;
  }

  if (player->get_class_id() != class_id::PLAYER)
  {
    return false;
  }

  auto* localplayer = entity_list->get_localplayer();
  return localplayer != nullptr && player != localplayer;
}

} // namespace

bool setup_bones_hook(void* me, void* bone_to_world_out, int max_bones, int bone_mask, float current_time)
{
  if (setup_bones_original == nullptr)
  {
    return false;
  }

  auto* player = reinterpret_cast<Player*>(me);
  if (!should_use_cached_bones(player, bone_to_world_out))
  {
    return setup_bones_original(me, bone_to_world_out, max_bones, bone_mask, current_time);
  }

  const int offset = cached_bone_data_offset();
  if (offset <= 0)
  {
    return setup_bones_original(me, bone_to_world_out, max_bones, bone_mask, current_time);
  }

  auto* cached_bones = reinterpret_cast<cached_bone_data*>(reinterpret_cast<std::uintptr_t>(player) + offset);
  if (cached_bones == nullptr || cached_bones->data == nullptr || cached_bones->size <= 0)
  {
    return setup_bones_original(me, bone_to_world_out, max_bones, bone_mask, current_time);
  }

  if (max_bones < cached_bones->size)
  {
    return setup_bones_original(me, bone_to_world_out, max_bones, bone_mask, current_time);
  }

  std::memcpy(
    bone_to_world_out,
    cached_bones->data,
    static_cast<std::size_t>(cached_bones->size) * sizeof(matrix_3x4));
  return true;
}
