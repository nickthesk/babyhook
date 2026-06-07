/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/dispatch_user_message.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "games/tf2/sdk/bitbuf.hpp"
#include "features/automation/misc/misc.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"

#include <cstring>

bool (*dispatch_user_message_original)(void*, int, bf_read*);

namespace
{

constexpr int vgui_menu_user_message_type = 12;

[[nodiscard]] bool is_vgui_info_menu(const bf_read* message_data)
{
  if (message_data == nullptr || !message_data->is_valid() || message_data->data_bytes < 4 || message_data->data == nullptr) {
    return false;
  }

  return std::memcmp(message_data->data, "info", 4) == 0;
}

void close_welcome_menu()
{
  if (engine == nullptr) {
    return;
  }

  engine->client_cmd_unrestricted("closedwelcomemenu");
  engine->client_cmd_unrestricted("menuclosed");
}

}

bool dispatch_user_message_hook(void* me, int message_type, bf_read* message_data) {
  automation::controller().on_dispatch_user_message(message_type, message_data);
  const bool dont_close_motd =
      config.misc.automation.anti_motd_dont_close_during_warmup && automation::controller().is_warmup_active();
  if (config.misc.automation.anti_motd && !dont_close_motd && message_type == vgui_menu_user_message_type && is_vgui_info_menu(message_data)) {
    close_welcome_menu();
    return true;
  }

  return dispatch_user_message_original(me, message_type, message_data);
}
