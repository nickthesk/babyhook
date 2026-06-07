/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/hooks/map_info_menu_show_panel.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include <unistd.h>

#include "features/automation/misc/misc.hpp"
#include "features/menu/config.hpp"

void (*map_info_menu_show_panel_original)(void*, bool) = NULL;

void map_info_menu_show_panel_hook(void* me, bool show) {
  const bool dont_close_during_warmup =
      config.misc.automation.anti_motd_dont_close_during_warmup && automation::controller().is_warmup_active();

  if (config.misc.automation.anti_motd == true && !dont_close_during_warmup) {
    map_info_menu_show_panel_original(me, false);
  } else {
    map_info_menu_show_panel_original(me, show);
  }
}
