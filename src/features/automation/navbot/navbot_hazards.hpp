/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_hazards.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef NAVBOT_HAZARDS_HPP
#define NAVBOT_HAZARDS_HPP

#include <vector>

#include "features/automation/navbot/navbot_types.hpp"

namespace navbot
{

class navbot_hazards
{
public:
  void clear();
  void update_expired(float current_time);
  void clear_soft_costs();
  void add_area_hazard(const hazard_record& record);
  void add_edge_hazard(const hazard_record& record);
  void add_transition_failure(nav_edge_id edge_id, float current_time, float duration);
  void refresh_crumb_blacklists(float current_time, float duration);
  void add_crumb_blacklist(nav_area_id area_id, nav_edge_id edge_id, float current_time, float duration);

  [[nodiscard]] bool is_area_blocked(nav_area_id area_id, float current_time) const;
  [[nodiscard]] bool is_edge_blocked(nav_edge_id edge_id, float current_time) const;
  [[nodiscard]] float area_cost(nav_area_id area_id, float current_time) const;
  [[nodiscard]] bool has_active_world_hazard(float current_time) const;
  [[nodiscard]] uint32_t generation() const;
  [[nodiscard]] const std::vector<hazard_record>& records() const;

private:
  uint32_t generation_ = 0;
  std::vector<hazard_record> records_{};
};

} // namespace navbot

#endif
