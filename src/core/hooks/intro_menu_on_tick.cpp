/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/hooks/intro_menu_on_tick.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include <unistd.h>

#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"

#include "games/tf2/sdk/entities/player.hpp"

#include "core/print.hpp"
#include "features/automation/misc/misc.hpp"

void (*intro_menu_on_tick_original)(void*) = NULL;

static float last_time2 = 0.0;

void intro_menu_on_tick_hook(void* me) {
  intro_menu_on_tick_original(me);
  automation::controller().on_menu_tick();
  return;

  /*
  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr) {
    intro_menu_on_tick_original(me);
    return;
  }

  
  // Init start time
  if (last_time2 == 0.0) {
    last_time2 = global_vars->curtime;
  }

  if (global_vars->curtime - last_time2 >= 1) {

    if (localplayer->get_tf_class() == tf_class::UNDEFINED) {
      *(int*)((unsigned long)(me) + 876) = 3;
      *(float*)((unsigned long)(me) + 872) = global_vars->curtime - 1;
    }
    
    last_time2 = global_vars->curtime;

    intro_menu_on_tick_original(me);

    if (localplayer->get_team() == tf_team::UNKNOWN) {
      engine->client_cmd_unrestricted("autoteam");
      engine->client_cmd_unrestricted("menuclosed");
    }
    
    engine->client_cmd_unrestricted("join_class sniper");
    return;
  }

  intro_menu_on_tick_original(me);
  */
  
}
