/*
/^-----^\   data: 2026-05-06
V  o o  V  file: src/core/hooks/region_selector.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef REGION_SELECTOR_HOOK_HPP
#define REGION_SELECTOR_HOOK_HPP

#include <cstdint>

#include "games/tf2/sdk/interfaces/steam_networking_utils.hpp"

inline int (*steam_networking_utils_get_ping_to_data_center_original)(
  void* self,
  steam_networking_pop_id pop_id,
  steam_networking_pop_id* via_relay_pop) = nullptr;

inline int (*steam_networking_utils_get_direct_ping_to_pop_original)(
  void* self,
  steam_networking_pop_id pop_id) = nullptr;

inline void (*region_selector_request_queue_for_match_original)(void* self, unsigned int match_group) = nullptr;

int steam_networking_utils_get_ping_to_data_center_hook(
  void* self,
  steam_networking_pop_id pop_id,
  steam_networking_pop_id* via_relay_pop);
int steam_networking_utils_get_direct_ping_to_pop_hook(void* self, steam_networking_pop_id pop_id);
bool region_selector_request_queue_for_match_available();
void request_queue_for_match_with_region_selector(void* self, unsigned int match_group);
void region_selector_request_queue_for_match_hook(void* self, unsigned int match_group);
bool install_steam_networking_utils_hooks();

#endif
