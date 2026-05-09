/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/hooks/client_create_move.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/input.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/mdl_cache.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"
#include "games/tf2/sdk/interfaces/steam_friends.hpp"

#include "games/tf2/sdk/entities/player.hpp"

#include "core/entity_cache.hpp"
#include "core/detach.hpp"
#include "features/automation/medic_automation/medic_automation.hpp"
#include "features/combat/anti_aim/anti_aim.hpp"
#include "features/combat/random_crits/random_crits.hpp"
#include "features/combat/tickbase/tickbase.hpp"

void (*client_create_move_original)(void*, int, float, bool);
using auto_allow_bone_access_fn = void (*)(void*, bool, bool);
using auto_allow_bone_access_on_delete_fn = void (*)(void*);

auto_allow_bone_access_fn auto_allow_bone_access_original = nullptr;
auto_allow_bone_access_on_delete_fn auto_allow_bone_access_on_delete_original = nullptr;

namespace
{

struct scoped_bone_access {
  scoped_bone_access() {
    active = auto_allow_bone_access_original != nullptr &&
             auto_allow_bone_access_on_delete_original != nullptr;
    if (active) {
      auto_allow_bone_access_original(auto_allow_storage, true, false);
    }
  }

  ~scoped_bone_access() {
    if (active) {
      auto_allow_bone_access_on_delete_original(auto_allow_storage);
    }
  }

  char auto_allow_storage[16]{};
  bool active = false;
};

struct scoped_mdl_cache_lock {
  scoped_mdl_cache_lock() {
    active = mdl_cache != nullptr;
    if (active) {
      mdl_cache->begin_lock();
    }
  }

  ~scoped_mdl_cache_lock() {
    if (active) {
      mdl_cache->end_lock();
    }
  }

  bool active = false;
};

struct scoped_client_create_move_features {
  scoped_client_create_move_features() {
    g_client_create_move_owns_features = true;
  }

  ~scoped_client_create_move_features() {
    g_client_create_move_owns_features = false;
  }
};

bool create_input_move(int sequence_number, float input_sample_frametime, bool active)
{
  if (input == nullptr) {
    return false;
  }

  const bool input_active = client_state != nullptr ? !client_state->m_bPaused : active;
  scoped_bone_access bone_access{};
  scoped_mdl_cache_lock mdl_cache_lock{};
  scoped_client_create_move_features feature_owner{};
  input->create_move(sequence_number, input_sample_frametime, input_active);
  return true;
}

void refresh_prediction_state()
{
  if (prediction == nullptr || client_state == nullptr) {
    return;
  }

  prediction->update(
    client_state->m_nDeltaTick,
    client_state->m_nDeltaTick > 0,
    client_state->last_command_ack,
    client_state->lastoutgoingcommand + client_state->chokedcommands);
}

unsigned int crc32_process_byte(unsigned int crc, unsigned char value)
{
  crc ^= value;
  for (int bit = 0; bit < 8; ++bit) {
    const unsigned int mask = 0U - (crc & 1U);
    crc = (crc >> 1) ^ (0xEDB88320U & mask);
  }

  return crc;
}

unsigned int crc32_process_buffer(unsigned int crc, const void* data, int size)
{
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (int i = 0; i < size; ++i) {
    crc = crc32_process_byte(crc, bytes[i]);
  }

  return crc;
}

unsigned int user_cmd_checksum(const user_cmd& cmd)
{
  unsigned int crc = 0xFFFFFFFFU;
  crc = crc32_process_buffer(crc, &cmd.command_number, sizeof(cmd.command_number));
  crc = crc32_process_buffer(crc, &cmd.tick_count, sizeof(cmd.tick_count));
  crc = crc32_process_buffer(crc, &cmd.view_angles, sizeof(cmd.view_angles));
  crc = crc32_process_buffer(crc, &cmd.forwardmove, sizeof(cmd.forwardmove));
  crc = crc32_process_buffer(crc, &cmd.sidemove, sizeof(cmd.sidemove));
  crc = crc32_process_buffer(crc, &cmd.upmove, sizeof(cmd.upmove));
  crc = crc32_process_buffer(crc, &cmd.buttons, sizeof(cmd.buttons));
  crc = crc32_process_buffer(crc, &cmd.impulse, sizeof(cmd.impulse));
  crc = crc32_process_buffer(crc, &cmd.weapon_select, sizeof(cmd.weapon_select));
  crc = crc32_process_buffer(crc, &cmd.weapon_subtype, sizeof(cmd.weapon_subtype));
  crc = crc32_process_buffer(crc, &cmd.random_seed, sizeof(cmd.random_seed));
  crc = crc32_process_buffer(crc, &cmd.mouse_dx, sizeof(cmd.mouse_dx));
  crc = crc32_process_buffer(crc, &cmd.mouse_dy, sizeof(cmd.mouse_dy));
  return crc ^ 0xFFFFFFFFU;
}

void update_verified_user_cmd(int sequence_number, user_cmd* cmd)
{
  if (input == nullptr || cmd == nullptr) {
    return;
  }

  auto* verified_cmd = input->get_verified_user_cmd(sequence_number);
  if (verified_cmd == nullptr) {
    return;
  }

  verified_cmd->cmd = *cmd;
  verified_cmd->crc = user_cmd_checksum(*cmd);
}

} // namespace



void client_create_move_hook(void* me, int sequence_number, float input_sample_frametime, bool active) {
  if (!create_input_move(sequence_number, input_sample_frametime, active)) {
    client_create_move_original(me, sequence_number, input_sample_frametime, active);
  }

  if (cathook::core::is_detach_pending()) {
    cathook::core::service_detach_request();
    return;
  }

  if (input == nullptr) {
    return;
  }

  auto* user_cmd = input->get_user_cmd(sequence_number);
  if (user_cmd == nullptr) {
    return;
  }

  refresh_prediction_state();
  cat_bind::run();
  automation::controller().on_create_move(user_cmd);

  if (can_run_move_features(user_cmd)) {
    Player* localplayer = entity_list->get_localplayer();
    if (should_run_taunt_slide(localplayer)) {
      user_cmd->buttons &= ~(IN_ATTACK | IN_ATTACK2 | IN_ATTACK3);
    } else {
      update_player_head_emoji_cache();
      run_move_features(user_cmd);
    }
  }

  if (!medic_automation::controller().should_suppress_random_crits()) {
    random_crits::run(user_cmd);
  }
  tickbase::on_create_move(user_cmd);
  anti_aim::on_create_move(user_cmd);
  update_verified_user_cmd(sequence_number, user_cmd);
  
  /*
  user_cmd* user_cmd = input->get_user_cmd(sequence_number);
  if (user_cmd == nullptr) {
    print("user_cmd == nullptr\n");
    return;
  }
  */
  
  /*
  prediction->update(client_state->m_nDeltaTick, client_state->m_nDeltaTick > 0, client_state->last_command_ack,
		     client_state->lastoutgoingcommand + client_state->chokedcommands);
  */
  
  //print("%p - %d - %d - %d - %f - %d\n", user_cmd, user_cmd->tick_count, sequence_number, sequence_number%90, input_sample_frametime, active);

  //user_cmd->random_seed = MD5_PseudoRandom(user_cmd->command_number) & 0x7FFFFFFF;

  //bhop(user_cmd);

  //print("%d\n", user_cmd->buttons);
  
  //print("client create move hooked!\n");
}
