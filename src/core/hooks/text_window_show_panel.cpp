/*
/^-----^\   data: 2026-06-03
|V  o o  V  file: src/core/hooks/text_window_show_panel.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include <unistd.h>

#include "features/menu/config.hpp"
#include "features/automation/misc/misc.hpp"

void (*text_window_show_panel_original)(void*, bool) = NULL;

void text_window_show_panel_hook(void* me, bool show) {
  if (automation::controller().anti_motd_handle_show_panel())
    return;

  text_window_show_panel_original(me, show);
}
