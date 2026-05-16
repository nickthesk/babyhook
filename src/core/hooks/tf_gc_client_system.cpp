/*
/^-----^\   data: 2026-05-08
V  o o  V  file: src/core/hooks/tf_gc_client_system.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "tf_gc_client_system.hpp"

#include <cstdint>

#include "features/menu/config.hpp"

namespace
{

constexpr int shared_object_created_event = 0;
constexpr unsigned int tf_game_server_lobby_type = 2004;
constexpr unsigned int tf_lobby_invite_type = 2008;
constexpr int shared_object_type_vfunc_index = 2;
constexpr int lobby_invite_id_vfunc_index = 12;

#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE
constexpr bool textmode_auto_casual_join = true;
#else
constexpr bool textmode_auto_casual_join = false;
#endif

using shared_object_type_fn = unsigned int (*)(void* self);
using lobby_invite_id_fn = std::uint64_t (*)(void* self);

bool auto_casual_join_enabled()
{
  return textmode_auto_casual_join || config.misc.automation.auto_casual_join;
}

unsigned int get_shared_object_type(void* shared_object)
{
  if (shared_object == nullptr)
  {
    return 0;
  }

  auto** vtable = *reinterpret_cast<void***>(shared_object);
  if (vtable == nullptr || vtable[shared_object_type_vfunc_index] == nullptr)
  {
    return 0;
  }

  auto get_type = reinterpret_cast<shared_object_type_fn>(vtable[shared_object_type_vfunc_index]);
  return get_type(shared_object);
}

std::uint64_t get_lobby_invite_id(void* shared_object)
{
  if (shared_object == nullptr)
  {
    return 0;
  }

  auto** vtable = *reinterpret_cast<void***>(shared_object);
  if (vtable == nullptr || vtable[lobby_invite_id_vfunc_index] == nullptr)
  {
    return 0;
  }

  auto get_lobby_id = reinterpret_cast<lobby_invite_id_fn>(vtable[lobby_invite_id_vfunc_index]);
  return get_lobby_id(shared_object);
}

void accept_lobby_invite(void* self, void* shared_object)
{
  if (tf_gc_client_system_request_accept_match_invite == nullptr)
  {
    return;
  }

  const std::uint64_t lobby_id = get_lobby_invite_id(shared_object);
  if (lobby_id == 0)
  {
    return;
  }

  tf_gc_client_system_request_accept_match_invite(self, lobby_id);
}

void join_matchmade_lobby(void* self)
{
  if (tf_gc_client_system_join_mm_match == nullptr)
  {
    return;
  }

  tf_gc_client_system_join_mm_match(self);
}

void call_original_so_event(void* self, void* shared_object, const int event_type)
{
  if (tf_gc_client_system_so_event_original == nullptr)
  {
    return;
  }

  tf_gc_client_system_so_event_original(self, shared_object, event_type);
}

} // namespace

void tf_gc_client_system_so_event_hook(void* self, void* shared_object, const int event_type)
{
  const unsigned int object_type = get_shared_object_type(shared_object);
  const bool should_auto_join =
    auto_casual_join_enabled() &&
    event_type == shared_object_created_event;

  if (should_auto_join && object_type == tf_lobby_invite_type)
  {
    accept_lobby_invite(self, shared_object);
  }

  call_original_so_event(self, shared_object, event_type);

  if (!should_auto_join)
  {
    return;
  }

  if (object_type == tf_game_server_lobby_type)
  {
    join_matchmade_lobby(self);
  }
}
