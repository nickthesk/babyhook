/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/menu/indicators.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef INDICATORS_HPP
#define INDICATORS_HPP

#include "config.hpp"
#include "binds.hpp"
#include "menu.hpp"

#include "features/combat/aimbot/aimbot_debug.hpp"
#include "features/combat/random_crits/random_crits.hpp"
#include "features/combat/tickbase/tickbase.hpp"
#include "features/visuals/spectator_list.hpp"

#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"

#include "imgui/dearimgui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace cat_indicator
{

inline constexpr float random_crits_width = 300.0f;
inline constexpr float random_crits_height = 190.0f;
inline constexpr float random_crits_compact_width = 180.0f;
inline constexpr float random_crits_compact_height = 29.0f;

enum class section_kind
{
  random_crits,
  tickbase,
  keybinds,
  spectators,
  aimbot_debug
};

struct section_spec
{
  section_kind kind = section_kind::keybinds;
  float width = 0.0f;
  float height = 0.0f;
  ImVec2 position{};
};

inline bool should_draw_overlay()
{
  if (menu_focused) {
    return true;
  }

  if (engine != nullptr && !engine->is_in_game()) {
    return false;
  }

  if (entity_list == nullptr) {
    return true;
  }

  Player* localplayer = entity_list->get_localplayer();
  return localplayer == nullptr || localplayer->is_alive();
}

inline bool has_indicator(const uint32_t flag)
{
  return (config.visuals.indicators.enabled_mask & flag) != 0;
}

inline ImU32 spectator_name_color(const spectator_list::spectator_entry& spectator)
{
  if (config.visuals.spectator_list.highlight_firstperson && spectator.firstperson) {
    auto firstperson_color = config.visuals.spectator_list.firstperson_color.to_RGBA();
    return IM_COL32(firstperson_color.r, firstperson_color.g, firstperson_color.b, firstperson_color.a);
  }

  return ImGui::GetColorU32(cat_menu::k_text);
}

inline void draw_compact_meter(
  ImDrawList* draw_list,
  const ImVec2 position,
  const float container_width,
  const std::string& left_text,
  const std::string& right_text,
  const float progress,
  const ImU32 bar_color)
{
  constexpr ImVec2 size(180.0f, 29.0f);
  constexpr float bar_height = 3.0f;
  const float text_height = size.y - bar_height;
  const float box_x = position.x + ((container_width - size.x) * 0.5f);
  const ImVec2 box_position(box_x, position.y);
  const ImU32 background_color = IM_COL32(0, 0, 0, 180);
  const ImU32 text_color = IM_COL32(255, 255, 255, 255);

  draw_list->AddRectFilled(box_position, ImVec2(box_position.x + size.x, box_position.y + text_height), background_color, 0.0f);
  draw_list->AddRectFilled(ImVec2(box_position.x, box_position.y + text_height), ImVec2(box_position.x + size.x, box_position.y + size.y), background_color, 0.0f);

  const float clamped_progress = std::clamp(progress, 0.0f, 1.0f);
  const float fill_width = size.x * clamped_progress;
  if (fill_width > 0.0f) {
    draw_list->AddRectFilled(
      ImVec2(box_position.x, box_position.y + text_height),
      ImVec2(box_position.x + fill_width, box_position.y + size.y),
      bar_color,
      0.0f);
  }

  draw_list->AddText(
    ImVec2(box_position.x + 5.0f, box_position.y + ((text_height - ImGui::GetTextLineHeight()) * 0.5f)),
    text_color,
    left_text.c_str());

  if (!right_text.empty()) {
    const ImVec2 right_size = ImGui::CalcTextSize(right_text.c_str());
    draw_list->AddText(
      ImVec2(box_position.x + size.x - right_size.x - 5.0f, box_position.y + ((text_height - ImGui::GetTextLineHeight()) * 0.5f)),
      bar_color,
      right_text.c_str());
  }
}

inline void draw_panel_box(ImDrawList* draw_list, const ImVec2 position, const ImVec2 size)
{
  draw_list->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), ImGui::GetColorU32(cat_menu::with_alpha(cat_menu::k_bg_panel, 0.96f)), 0.0f);
  draw_list->AddRect(position, ImVec2(position.x + size.x, position.y + size.y), ImGui::GetColorU32(cat_menu::k_line), 0.0f, 0, 1.0f);
}

inline auto collect_keybind_rows() -> std::vector<cat_bind::indicator_row>
{
  std::vector<cat_bind::indicator_row> rows = cat_bind::collect_indicator_rows();
  if (rows.empty() && menu_focused) {
    rows.push_back({
      .label = "No binds",
      .key = "",
      .state = "",
      .target_key = "",
      .popup_type = cat_bind::popup_target_type::value_bind,
      .active = false
    });
  }

  return rows;
}

inline auto collect_spectator_rows(Player** target_player_out) -> std::vector<spectator_list::spectator_entry>
{
  std::vector<spectator_list::spectator_entry> spectators = spectator_list::collect_spectators(target_player_out);
  if (spectators.empty() && menu_focused) {
    spectators.push_back({
      .name = "No spectators",
      .firstperson = false
    });
  }

  return spectators;
}

inline auto build_sections() -> std::vector<section_spec>
{
  std::vector<section_spec> sections{};
  sections.reserve(5);

  if (has_indicator(Visuals::Indicators::random_crits)) {
    const bool advanced_stats = config.random_crits.advanced_stats;
    sections.push_back({
      .kind = section_kind::random_crits,
      .width = advanced_stats ? random_crits_width : random_crits_compact_width,
      .height = advanced_stats ? random_crits_height : random_crits_compact_height,
      .position = ImVec2(config.visuals.indicators.random_crits_x, config.visuals.indicators.random_crits_y)
    });
  }

  if (has_indicator(Visuals::Indicators::tickbase) && (menu_focused || config.misc.exploits.tickbase)) {
    sections.push_back({
      .kind = section_kind::tickbase,
      .width = 180.0f,
      .height = 29.0f,
      .position = ImVec2(config.visuals.indicators.legacy_ticks_x, config.visuals.indicators.legacy_ticks_y)
    });
  }

  if (has_indicator(Visuals::Indicators::keybinds)) {
    const std::vector<cat_bind::indicator_row> rows = collect_keybind_rows();
    if (!rows.empty()) {
      sections.push_back({
        .kind = section_kind::keybinds,
        .width = 240.0f,
        .height = 28.0f + (18.0f * static_cast<float>(rows.size())) + 8.0f,
        .position = ImVec2(config.visuals.indicators.keybinds_x, config.visuals.indicators.keybinds_y)
      });
    }
  }

  if (has_indicator(Visuals::Indicators::spectators)) {
    Player* target_player = nullptr;
    const std::vector<spectator_list::spectator_entry> spectators = collect_spectator_rows(&target_player);
    if (!spectators.empty()) {
      sections.push_back({
        .kind = section_kind::spectators,
        .width = 250.0f,
        .height = 28.0f + (18.0f * static_cast<float>(spectators.size())) + 8.0f,
        .position = ImVec2(config.visuals.spectator_list.x, config.visuals.spectator_list.y)
      });
    }
  }

  if (config.aimbot.debug_overlay) {
    const aimbot_debug_state& state = aimbot_debug_get_state();
    if (menu_focused || state.active) {
      sections.push_back({
        .kind = section_kind::aimbot_debug,
        .width = 300.0f,
        .height = 174.0f,
        .position = ImVec2(config.aimbot.debug_overlay_x, config.aimbot.debug_overlay_y)
      });
    }
  }

  return sections;
}

struct section_position_refs
{
  float* x = nullptr;
  float* y = nullptr;
};

inline auto section_drag_position(section_kind kind) -> section_position_refs
{
  switch (kind) {
  case section_kind::random_crits:
    return { .x = &config.visuals.indicators.random_crits_x, .y = &config.visuals.indicators.random_crits_y };
  case section_kind::tickbase:
    return { .x = &config.visuals.indicators.legacy_ticks_x, .y = &config.visuals.indicators.legacy_ticks_y };
  case section_kind::keybinds:
    return { .x = &config.visuals.indicators.keybinds_x, .y = &config.visuals.indicators.keybinds_y };
  case section_kind::spectators:
    return { .x = &config.visuals.spectator_list.x, .y = &config.visuals.spectator_list.y };
  case section_kind::aimbot_debug:
    return { .x = &config.aimbot.debug_overlay_x, .y = &config.aimbot.debug_overlay_y };
  }

  return {};
}

inline void handle_drag_window(const section_spec& section)
{
  if (!menu_focused) {
    return;
  }

  const section_position_refs position = section_drag_position(section.kind);
  if (position.x == nullptr || position.y == nullptr) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(*position.x, *position.y), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(section.width, section.height), ImGuiCond_Always);
  constexpr ImGuiWindowFlags flags =
    ImGuiWindowFlags_NoDecoration |
    ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoBackground |
    ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoNav;

  const std::string window_name = "indicator_drag_" + std::to_string(static_cast<int>(section.kind));
  if (!ImGui::Begin(window_name.c_str(), nullptr, flags)) {
    ImGui::End();
    return;
  }

  const float drag_height = section.kind == section_kind::keybinds || section.kind == section_kind::spectators ? 28.0f : section.height;
  ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
  ImGui::InvisibleButton("##indicator_drag", ImVec2(section.width, drag_height));
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    *position.x += delta.x;
    *position.y += delta.y;
  }

  if (section.kind == section_kind::keybinds) {
    const std::vector<cat_bind::indicator_row> rows = collect_keybind_rows();
    constexpr float row_height = 18.0f;
    constexpr float header_height = 28.0f;
    float row_y = header_height;
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const auto& row = rows[index];
      if (row.target_key.empty()) {
        row_y += row_height;
        continue;
      }

      ImGui::SetCursorPos(ImVec2(0.0f, row_y));
      ImGui::PushID(static_cast<int>(index));
      ImGui::InvisibleButton("##keybind_indicator_row", ImVec2(section.width, row_height), ImGuiButtonFlags_MouseButtonRight);
      if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        cat_bind::request_popup(row.target_key, row.popup_type);
      }
      ImGui::PopID();
      row_y += row_height;
    }
  }

  ImGui::End();
}

inline auto crit_indicator_color(const random_crits::indicator_state& state) -> ImU32
{
  if (!state.available || !state.enabled) {
    return ImGui::GetColorU32(cat_menu::k_text_muted);
  }

  if (!state.can_random_crit || state.crit_banned) {
    return IM_COL32(200, 55, 55, 255);
  }

  if (state.crit_boosted || state.forcing) {
    const auto color = config.visuals.hitmarker.crit_color.to_RGBA();
    return IM_COL32(color.r, color.g, color.b, color.a);
  }

  if (state.rapid_wait > 0.0f) {
    return IM_COL32(255, 150, 0, 255);
  }

  if (state.skipping || state.save_mode) {
    return ImGui::GetColorU32(cat_menu::k_accent);
  }

  return ImGui::GetColorU32(cat_menu::k_text);
}

inline auto build_crit_status_text(const random_crits::indicator_state& state) -> std::string
{
  const bool has_next_crit_seed = state.next_crit_seed_roll >= 0;

  if (!state.enabled) {
    return "off";
  }
  if (!state.available) {
    return "no weapon";
  }
  if (!state.can_random_crit) {
    return "disabled";
  }
  if (state.crit_boosted) {
    return "boosted";
  }
  if (state.streaming_time > 0.0f) {
    return "streaming";
  }
  if (state.crit_banned) {
    return "banned";
  }
  if (state.forcing) {
    return "forcing";
  }
  if (state.skipping) {
    return "saving";
  }
  if (state.rapid_wait > 0.0f) {
    return "waiting";
  }
  if (state.force_mode || (config.random_crits.always_melee_crit && state.melee_weapon)) {
    if (!state.can_attack || state.available_crits <= 0) {
      return "building";
    }

    return has_next_crit_seed ? "seed found" : "scanning";
  }
  if (state.save_mode) {
    return "saving";
  }

  return "idle";
}

inline auto format_float(const float value, const char* format) -> std::string
{
  char buffer[64]{};
  std::snprintf(buffer, sizeof(buffer), format, value);
  return buffer;
}

inline auto build_crit_rows(const random_crits::indicator_state& state) -> std::vector<std::pair<std::string, std::string>>
{
  std::vector<std::pair<std::string, std::string>> rows{};
  rows.reserve(7);

  if (!state.available) {
    rows.emplace_back("weapon", "none");
    return rows;
  }

  rows.emplace_back("affords", std::to_string(std::max(0, state.available_crits)) + " / " + std::to_string(std::max(0, state.potential_crits)));
  if (state.next_crit_seed_roll >= 0) {
    rows.emplace_back("crit seed", "+" + std::to_string(state.next_crit_seed_offset) + " roll " + std::to_string(state.next_crit_seed_roll));
  } else if (state.available_crits > 0) {
    rows.emplace_back("crit seed", "scan miss");
  } else {
    rows.emplace_back("build", state.next_crit > 0 ? std::to_string(state.next_crit) + " shots" : "need dmg");
  }
  rows.emplace_back("bucket", std::to_string(static_cast<int>(state.bucket)) + " / " + std::to_string(static_cast<int>(state.bucket_cap)));
  rows.emplace_back("cost", std::to_string(static_cast<int>(std::ceil(state.crit_cost))) + " (" + std::to_string(static_cast<int>(std::ceil(state.damage_to_crit))) + " dmg)");
  rows.emplace_back("chance", format_float(state.crit_chance * 100.0f, "%.1f%%") + " x" + format_float(state.crit_chance_mult, "%.2f"));
  rows.emplace_back("seeds", std::to_string(state.seed_requests) + " / " + std::to_string(state.checks) + " scan " + std::to_string(state.seed_scan));

  if (state.selected) {
    rows.emplace_back("cmd", "+" + std::to_string(state.selected_offset) + " roll " + std::to_string(state.selected_roll));
  } else if (state.rapid_wait > 0.0f) {
    rows.emplace_back("wait", format_float(state.rapid_wait, "%.2fs"));
  } else if (state.streaming_time > 0.0f) {
    rows.emplace_back("stream", format_float(state.streaming_time, "%.2fs"));
  } else {
    rows.emplace_back("seed", std::to_string(state.current_seed));
  }

  return rows;
}

inline void draw_info_row(ImDrawList* draw_list, const ImVec2 position, const float width, const char* label, const std::string& value, const ImU32 value_color)
{
  const ImU32 label_color = ImGui::GetColorU32(cat_menu::k_text_muted);
  draw_list->AddText(ImVec2(position.x + 12.0f, position.y), label_color, label);

  std::string clipped_value = value;
  const float max_width = width - 96.0f;
  while (!clipped_value.empty() && ImGui::CalcTextSize(clipped_value.c_str()).x > max_width) {
    clipped_value.pop_back();
  }
  if (clipped_value.size() < value.size() && clipped_value.size() > 3) {
    clipped_value.resize(clipped_value.size() - 3);
    clipped_value += "...";
  }

  const ImVec2 value_size = ImGui::CalcTextSize(clipped_value.c_str());
  draw_list->AddText(ImVec2(position.x + width - value_size.x - 12.0f, position.y), value_color, clipped_value.c_str());
}

inline auto bool_text(const bool value) -> const char*
{
  return value ? "yes" : "no";
}

inline void draw_aimbot_debug_section(ImDrawList* draw_list, const ImVec2 position)
{
  const aimbot_debug_state& state = aimbot_debug_get_state();
  constexpr float width = 300.0f;
  constexpr float height = 174.0f;
  constexpr float row_height = 17.0f;
  draw_panel_box(draw_list, position, ImVec2(width, height));

  const ImVec2 title_size = ImGui::CalcTextSize("Aimbot");
  draw_list->AddText(ImVec2(position.x + 8.0f, position.y + 7.0f), ImGui::GetColorU32(cat_menu::k_text), "Aimbot");
  draw_list->AddText(
    ImVec2(position.x + 8.0f + title_size.x, position.y + 7.0f),
    state.attack_ready ? ImGui::GetColorU32(cat_menu::k_accent) : IM_COL32(255, 150, 0, 255),
    " debug");

  const ImU32 value_color = state.attack_ready
    ? ImGui::GetColorU32(cat_menu::k_accent)
    : IM_COL32(255, 150, 0, 255);
  float row_y = position.y + 31.0f;
  draw_info_row(draw_list, ImVec2(position.x, row_y), width, "reason", aimbot_debug_reason_name(state.reason), value_color);
  row_y += row_height;
  draw_info_row(draw_list, ImVec2(position.x, row_y), width, "target", std::to_string(state.selected_entity_index) + " hb " + std::to_string(state.selected_hitbox), value_color);
  row_y += row_height;
  draw_info_row(draw_list, ImVec2(position.x, row_y), width, "trace", std::to_string(state.trace_entity_index) + " hb " + std::to_string(state.trace_hitbox), value_color);
  row_y += row_height;
  draw_info_row(draw_list, ImVec2(position.x, row_y), width, "candidates", std::to_string(state.candidates_visible) + "/" + std::to_string(state.candidates_total) + " reject " + std::to_string(state.candidates_rejected), value_color);
  row_y += row_height;
  draw_info_row(draw_list, ImVec2(position.x, row_y), width, "scope/head", std::string(bool_text(state.scoped_ready)) + " / " + bool_text(state.headshot_ready), value_color);
  row_y += row_height;
  draw_info_row(draw_list, ImVec2(position.x, row_y), width, "spread", format_float(state.spread, "%.4f") + (state.spread_compensated ? " comp" : ""), value_color);
  row_y += row_height;
  draw_info_row(draw_list, ImVec2(position.x, row_y), width, "pellet", std::to_string(state.pellet_index) + " / " + std::to_string(state.pellet_count), value_color);
  row_y += row_height;
  draw_info_row(draw_list, ImVec2(position.x, row_y), width, "tick/fov", std::to_string(state.tick_count) + " / " + format_float(state.fov, "%.2f"), value_color);
}

inline void draw_random_crits_section(ImDrawList* draw_list, const ImVec2 position)
{
  const float width = config.random_crits.advanced_stats ? random_crits_width : random_crits_compact_width;
  const float height = config.random_crits.advanced_stats ? random_crits_height : random_crits_compact_height;
  constexpr float row_height = 17.0f;
  const auto state = random_crits::get_indicator_state();
  const auto bar_color = crit_indicator_color(state);
  const auto status = build_crit_status_text(state);
  const auto bucket_text = state.available ? std::to_string(std::max(0, state.available_crits)) + "/" + std::to_string(std::max(0, state.potential_crits)) : std::string{};

  if (!config.random_crits.advanced_stats) {
    draw_compact_meter(draw_list, position, width, status, bucket_text, state.bucket_progress, bar_color);
    return;
  }

  const auto rows = build_crit_rows(state);
  draw_panel_box(draw_list, position, ImVec2(width, height));

  const ImVec2 crit_size = ImGui::CalcTextSize("Crit");
  draw_list->AddText(ImVec2(position.x + 8.0f, position.y + 7.0f), ImGui::GetColorU32(cat_menu::k_text), "Crit");
  draw_list->AddText(ImVec2(position.x + 8.0f + crit_size.x, position.y + 7.0f), bar_color, "s");
  draw_compact_meter(draw_list, ImVec2(position.x, position.y + 27.0f), width, status, bucket_text, state.bucket_progress, bar_color);

  float row_y = position.y + 64.0f;
  for (const auto& [label, value] : rows) {
    draw_info_row(draw_list, ImVec2(position.x, row_y), width, label.c_str(), value, bar_color);
    row_y += row_height;
  }
}

inline auto tickbase_indicator_color(const tickbase::indicator_state& state) -> ImU32
{
  if (!config.misc.exploits.tickbase) {
    return ImGui::GetColorU32(cat_menu::k_text_muted);
  }

  if (state.shifting || state.doubletap || state.warp) {
    return ImGui::GetColorU32(cat_menu::k_accent);
  }

  if (state.recharging) {
    return IM_COL32(255, 150, 0, 255);
  }

  if (state.available_shift_ticks > 0) {
    return IM_COL32(100, 220, 130, 255);
  }

  return ImGui::GetColorU32(cat_menu::k_text_muted);
}

inline auto build_tickbase_status_text(const tickbase::indicator_state& state) -> std::string
{
  if (!config.misc.exploits.tickbase) {
    return "off";
  }

  if (state.doubletap) {
    return "doubletap";
  }

  if (state.warp) {
    return "warp";
  }

  if (state.recharging) {
    return "recharge";
  }

  if (state.fakelag && state.choked_commands > 0) {
    return "fakelag";
  }

  return state.available_shift_ticks > 0 ? "ready" : "building";
}

inline void draw_tickbase_section(ImDrawList* draw_list, const ImVec2 position)
{
  const auto state = tickbase::get_indicator_state();
  const auto bar_color = tickbase_indicator_color(state);
  const auto status = build_tickbase_status_text(state);
  const int max_ticks = std::max(1, state.max_processing_ticks);
  const auto tick_text = std::to_string(std::clamp(state.processing_ticks, 0, max_ticks)) + "/" + std::to_string(max_ticks);
  const float progress = static_cast<float>(std::clamp(state.processing_ticks, 0, max_ticks)) / static_cast<float>(max_ticks);

  draw_compact_meter(draw_list, position, 180.0f, status, tick_text, progress, bar_color);
}

inline void draw_keybind_section(ImDrawList* draw_list, const ImVec2 position)
{
  const std::vector<cat_bind::indicator_row> rows = collect_keybind_rows();
  if (rows.empty()) {
    return;
  }

  constexpr float width = 240.0f;
  constexpr float row_height = 18.0f;
  constexpr float header_height = 28.0f;
  const float height = header_height + (row_height * static_cast<float>(rows.size())) + 8.0f;
  draw_panel_box(draw_list, position, ImVec2(width, height));

  const ImVec2 key_size = ImGui::CalcTextSize("Key");
  draw_list->AddText(ImVec2(position.x + 8.0f, position.y + 7.0f), ImGui::GetColorU32(cat_menu::k_text), "Key");
  draw_list->AddText(ImVec2(position.x + 8.0f + key_size.x, position.y + 7.0f), ImGui::GetColorU32(cat_menu::k_accent), "binds");

  float row_y = position.y + header_height;
  for (const cat_bind::indicator_row& row : rows) {
    const ImU32 row_name_color = row.active ? ImGui::GetColorU32(cat_menu::k_accent) : ImGui::GetColorU32(cat_menu::k_text);
    const ImU32 row_text_color = row.active ? ImGui::GetColorU32(cat_menu::k_text) : ImGui::GetColorU32(cat_menu::k_text_muted);
    const ImVec2 state_size = ImGui::CalcTextSize(row.state.c_str());
    const float key_x = position.x + 104.0f;
    const float state_x = position.x + width - state_size.x - 12.0f;
    const float max_key_width = std::max(0.0f, state_x - key_x - 8.0f);
    std::string key_text = row.key;

    while (!key_text.empty() && ImGui::CalcTextSize(key_text.c_str()).x > max_key_width) {
      key_text.pop_back();
    }
    if (key_text.size() < row.key.size() && key_text.size() > 3) {
      key_text.resize(key_text.size() - 3);
      key_text += "...";
    }

    draw_list->AddText(ImVec2(position.x + 12.0f, row_y), row_name_color, row.label.c_str());
    draw_list->AddText(ImVec2(key_x, row_y), row_text_color, key_text.c_str());
    draw_list->AddText(ImVec2(state_x, row_y), row.active ? ImGui::GetColorU32(cat_menu::k_accent) : ImGui::GetColorU32(cat_menu::k_text_muted), row.state.c_str());
    row_y += row_height;
  }
}

inline auto build_spectator_title(Player* target_player) -> std::string
{
  std::string title = "Spectators";
  if (!config.visuals.spectator_list.show_target || target_player == nullptr || engine == nullptr || entity_list == nullptr) {
    return title;
  }

  player_info info{};
  if (!engine->get_player_info(target_player->get_index(), &info)) {
    return title;
  }

  title += ": ";
  title += target_player == entity_list->get_localplayer() ? "you" : info.name;
  return title;
}

inline void draw_spectator_section(ImDrawList* draw_list, const ImVec2 position)
{
  Player* target_player = nullptr;
  const std::vector<spectator_list::spectator_entry> spectators = collect_spectator_rows(&target_player);
  if (spectators.empty()) {
    return;
  }

  constexpr float width = 250.0f;
  constexpr float row_height = 18.0f;
  constexpr float header_height = 28.0f;
  const float height = header_height + (row_height * static_cast<float>(spectators.size())) + 8.0f;
  draw_panel_box(draw_list, position, ImVec2(width, height));

  const std::string title = build_spectator_title(target_player);
  draw_list->AddText(ImVec2(position.x + 10.0f, position.y + 7.0f), ImGui::GetColorU32(cat_menu::k_text), title.c_str());

  float row_y = position.y + header_height;
  for (const spectator_list::spectator_entry& spectator : spectators) {
    const ImU32 name_color = spectator_name_color(spectator);
    draw_list->AddText(ImVec2(position.x + 12.0f, row_y), name_color, spectator.name.c_str());

    if (config.visuals.spectator_list.show_modes) {
      const char* mode_text = spectator.firstperson ? "1st" : "3rd";
      const ImVec2 mode_size = ImGui::CalcTextSize(mode_text);
      draw_list->AddText(
        ImVec2(position.x + width - mode_size.x - 12.0f, row_y),
        spectator.firstperson ? name_color : ImGui::GetColorU32(cat_menu::k_text_muted),
        mode_text);
    }

    row_y += row_height;
  }
}

} // namespace cat_indicator

static void draw_game_indicators()
{
  using namespace cat_indicator;

  if (!should_draw_overlay()) {
    return;
  }

  const std::vector<section_spec> sections = build_sections();
  if (sections.empty()) {
    return;
  }

  for (const section_spec& section : sections) {
    handle_drag_window(section);
  }

  ImDrawList* draw_list = ImGui::GetForegroundDrawList();
  for (const section_spec& section : sections) {
    switch (section.kind) {
    case section_kind::random_crits:
      draw_random_crits_section(draw_list, section.position);
      break;
    case section_kind::tickbase:
      draw_tickbase_section(draw_list, section.position);
      break;
    case section_kind::keybinds:
      draw_keybind_section(draw_list, section.position);
      break;
    case section_kind::spectators:
      draw_spectator_section(draw_list, section.position);
      break;
    case section_kind::aimbot_debug:
      draw_aimbot_debug_section(draw_list, section.position);
      break;
    }
  }
}

#endif
