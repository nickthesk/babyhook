/*
/^-----^\   data: 2026-05-05
V  o o  V  file: src/features/automation/autoitem/autoitem.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef AUTOITEM_HPP
#define AUTOITEM_HPP

#include <vector>

namespace autoitem
{

void initialize();
void on_tick();

bool rent_item(int item_def_id);
bool craft_items(const std::vector<int>& item_def_ids);
bool unlock_achievement_by_id(int achievement_id);
bool lock_achievement_by_id(int achievement_id);
bool dump_achievements(const char* path);

} // namespace autoitem

#endif
