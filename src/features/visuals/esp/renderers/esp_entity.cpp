/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/features/visuals/esp/renderers/esp_entity.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "../esp.hpp"

#include <array>
#include <cstdlib>
#include <cwchar>
#include <string>

#include "features/menu/config.hpp"
#include "features/visuals/overlay_projection.hpp"

#include "games/tf2/sdk/interfaces/surface.hpp"
#include "games/tf2/sdk/interfaces/render_view.hpp"

#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/building.hpp"

#include "core/types.hpp"
#include "core/print.hpp"

void box_esp_entity(Vec3 screen, Entity* entity, Player* localplayer) {
  if (config.esp.pickup.box == true && entity->get_pickup_type() != pickup_type::UNKNOWN) {
    if (entity->is_dormant()) return;
    surface->set_rgba(255, 255, 255, 255);
    surface->draw_outlined_rect(screen.x-5, screen.y-5, screen.x+5, screen.y+5);
  }

  if (config.esp.intelligence.box == true && entity->get_class_id() == class_id::CAPTURE_FLAG && entity->get_owner_entity() != localplayer) {
    Vec3 location = entity->get_origin();

    Vec3 screen_offset_top;
    Vec3 location_offset_top = {location.x, location.y, location.z + 6};
    if (!overlay_projection::world_to_screen(location_offset_top, &screen_offset_top)) return;

    Vec3 screen_offset_bottom;
    Vec3 location_offset_bottom = {location.x, location.y, location.z - 6};
    if (!overlay_projection::world_to_screen(location_offset_bottom, &screen_offset_bottom)) return;
    
    RGBA color = {255, 255, 255, 255};
    if (entity->get_team() == tf_team::BLU) color = RGBA{0, 0, 255, 255};
    if (entity->get_team() == tf_team::RED) color = RGBA{255, 0, 0, 255};
    draw_outline_rectangle(screen_offset_bottom, screen_offset_top, 1.75, color);
  }
  
  if (config.esp.buildings.box == true && entity->is_building() && (entity->get_team() != localplayer->get_team() || (entity->get_team() == localplayer->get_team() && config.esp.buildings.team == true))) {
    Building* building = (Building*)entity;
    if (building->is_carried()) return;
    if (entity->is_dormant()) return;
    
    Vec3 screen_offset;
    float box_offset_fraction = 0;
    
    switch (building->get_class_id()) {
    case class_id::DISPENSER:
      {
	Vec3 location = building->get_origin();
	Vec3 location_offset = {location.x, location.y, location.z + 58};
	if (!overlay_projection::world_to_screen(location_offset, &screen_offset)) return;

	box_offset_fraction = 0.25;
	break;
      }
    case class_id::SENTRY:
      {
	Vec3 location = building->get_origin();

	float z_offset = 0;
	switch (building->get_building_level()) {
	case 1:
	  z_offset = 45;
	  box_offset_fraction = 0.30;
	  break;
	case 2:
	  z_offset = 55;
	  box_offset_fraction = 0.60;
	  break;
	case 3:
	  z_offset = 75;
	  box_offset_fraction = 0.50;
	  break;
	}

	Vec3 location_offset = {location.x, location.y, location.z + z_offset};
	if (!overlay_projection::world_to_screen(location_offset, &screen_offset)) return;
	break;
      }
    case class_id::TELEPORTER:
      {
	Vec3 location = building->get_origin();
	Vec3 location_offset = {location.x, location.y, location.z + 15};
	if (!overlay_projection::world_to_screen(location_offset, &screen_offset)) return;

	box_offset_fraction = 1.8;
	break;
      }
    }

    RGBA color = config.esp.player.enemy_color.to_RGBA(); // Use player color for now
    if (building->get_team() == localplayer->get_team()) color = config.esp.player.team_color.to_RGBA();
    
    draw_outline_rectangle(screen, screen_offset, box_offset_fraction, color);
  }
}

void health_bar_esp_entity(Vec3 screen, Entity* entity, Player* localplayer) {
  if (config.esp.buildings.health_bar == true && entity->is_building() && (entity->get_team() != localplayer->get_team() || (entity->get_team() == localplayer->get_team() && config.esp.buildings.team == true))) {
    Building* building = (Building*)entity;
    if (building->is_carried()) return;
    if (entity->is_dormant()) return;

    float box_offset = 0;
    switch (building->get_class_id()) {
    case class_id::SENTRY:
      {
	Vec3 location = building->get_origin();

	float z_offset = 0;
	float box_offset_fraction = 0;
	switch (building->get_building_level()) {
	case 1:
	  z_offset = 45;
	  box_offset_fraction = 0.30;
	  break;
	case 2:
	  z_offset = 55;
	  box_offset_fraction = 0.60;
	  break;
	case 3:
	  z_offset = 75;
	  box_offset_fraction = 0.50;
	  break;
	}
    
	Vec3 location_offset = {location.x, location.y, location.z + z_offset};
	Vec3 screen_offset;
	if (!overlay_projection::world_to_screen(location_offset, &screen_offset)) return;

	box_offset = (screen.y - screen_offset.y)*box_offset_fraction;
	break;
      }
    case class_id::DISPENSER:
      {    
	Vec3 location = building->get_origin();
	Vec3 location_offset = {location.x, location.y, location.z + 58};
	Vec3 screen_offset;
	if (!overlay_projection::world_to_screen(location_offset, &screen_offset)) return;

	box_offset = (screen.y - screen_offset.y)*0.25;    
	break;
      }
    case class_id::TELEPORTER:
      {
    
	Vec3 location = building->get_origin();
	Vec3 location_offset = {location.x, location.y, location.z + 15};
	Vec3 screen_offset;
	if (!overlay_projection::world_to_screen(location_offset, &screen_offset)) return;

	box_offset = (screen.y - screen_offset.y)*1.8;
	break;
      }
    }

    surface->set_rgba(0, 0, 0, 255);
    surface->draw_line(screen.x - box_offset - 1, screen.y + 3, screen.x + box_offset + 2, screen.y + 3);
    surface->draw_line(screen.x - box_offset - 1, screen.y + 4, screen.x + box_offset + 2, screen.y + 4);
    surface->draw_line(screen.x - box_offset - 1, screen.y + 5, screen.x + box_offset + 2, screen.y + 5);
    
    int xdelta = (box_offset*2) * (1.f - (float(building->get_health()) / building->get_max_health()));    

    if (building->get_health() > building->get_max_health()) { // over healed (the building some how???)
      surface->set_rgba(0, 255, 255, 255);
      xdelta = 0;
    }
    else if (building->get_health() <= building->get_max_health() && building->get_health() >= (building->get_max_health()*.9))
      surface->set_rgba(0, 255, 0, 255);
    else if (building->get_health() < (building->get_max_health()*.9) && building->get_health() > (building->get_max_health()*.6))
      surface->set_rgba(90, 255, 0, 255);
    else if (building->get_health() <= (building->get_max_health()*.6) && building->get_health() > (building->get_max_health()*.35))
      surface->set_rgba(255, 100, 0, 255);
    else if (building->get_health() <= (building->get_max_health()*.35))
      surface->set_rgba(255, 0, 0, 255);
    
    surface->draw_line(screen.x - box_offset, screen.y + 4, screen.x + box_offset - xdelta + 1, screen.y + 4);
  }
}


void name_esp_entity(Vec3 screen, Entity* entity, Player* localplayer) {

  if (config.esp.pickup.name == true && entity->get_pickup_type() != pickup_type::UNKNOWN) {
    if (entity->is_dormant()) return;
    surface->draw_set_text_color(255, 255, 255, 255);
    
    if (entity->get_pickup_type() == pickup_type::AMMOPACK) {
      surface->draw_set_text_pos(screen.x - (surface->get_string_width(esp_entity_font, L"AMMO")*0.5), screen.y);
      surface->draw_print_text(L"AMMO", wcslen(L"AMMO"));
    } else if (entity->get_pickup_type() == pickup_type::MEDKIT) {
      surface->draw_set_text_color(0, 255, 25, 255);
      surface->draw_set_text_pos(screen.x - (surface->get_string_width(esp_entity_font, L"HEALTH")*0.5), screen.y);
      surface->draw_print_text(L"HEALTH", wcslen(L"HEALTH"));
    }
  }

  if (config.esp.intelligence.name == true && entity->get_class_id() == class_id::CAPTURE_FLAG) {
    Vec3 location = entity->get_origin();

    Vec3 screen_offset_bottom;
    Vec3 location_offset_bottom = {location.x, location.y, location.z - 6};
    if (!overlay_projection::world_to_screen(location_offset_bottom, &screen_offset_bottom)) return;
    
    RGBA color = {255, 255, 255, 255};
    if (entity->get_team() == tf_team::BLU) color = RGBA{0, 0, 255, 255};
    if (entity->get_team() == tf_team::RED) color = RGBA{255, 0, 0, 255};
    surface->draw_set_text_color(color);

    surface->draw_set_text_pos(screen_offset_bottom.x - surface->get_string_width(esp_entity_font, L"FLAG")*0.5, screen_offset_bottom.y + 1);
    surface->draw_print_text(L"FLAG", wcslen(L"FLAG"));
  }
  
  if (config.esp.buildings.name == true && entity->is_building() && (entity->get_team() != localplayer->get_team() || (entity->get_team() == localplayer->get_team() && config.esp.buildings.team == true))) {
    Building* building = (Building*)entity;
    if (building->is_carried()) return;
    if (entity->is_dormant()) return;
    
    surface->draw_set_text_color(255, 255, 255, 255);
    switch (building->get_class_id()) {
    case class_id::DISPENSER:
      {
	surface->draw_set_text_pos(screen.x - (surface->get_string_width(esp_entity_font, L"DISPENSER")*0.5), screen.y + (config.esp.buildings.health_bar ? 5 : 0));
	surface->draw_print_text(L"DISPENSER", wcslen(L"DISPENSER"));
	break;
      }
    case class_id::SENTRY:
      {
	surface->draw_set_text_pos(screen.x - (surface->get_string_width(esp_entity_font, L"SENTRY")*0.5), screen.y + (config.esp.buildings.health_bar ? 5 : 0));
	surface->draw_print_text(L"SENTRY", wcslen(L"SENTRY"));
	break;
      }
    case class_id::TELEPORTER:
      {
	surface->draw_set_text_pos(screen.x - (surface->get_string_width(esp_entity_font, L"TELEPORTER")*0.5), screen.y + (config.esp.buildings.health_bar ? 5 : 0));
	surface->draw_print_text(L"TELEPORTER", wcslen(L"TELEPORTER"));
	break;
      }
    }

  }
  
  if (config.debug.debug_render_all_entities == true) {
    std::string model_name = entity->get_model_name();
      
    std::array<wchar_t, 64> model_name_w{};
    const auto len = std::mbstowcs(model_name_w.data(), model_name.c_str(), model_name_w.size() - 1);
    if (len == static_cast<std::size_t>(-1)) return;

    std::wstring a
      = L"Model Path: " + std::wstring(model_name_w.data())
      + L"\nClass ID: " + std::to_wstring(entity->get_class_id())
      + L"\nEntity Index: " + std::to_wstring(entity->get_index());

    surface->draw_set_text_color(255, 255, 255, 255);
    surface->draw_set_text_pos(screen.x, screen.y);
    surface->draw_print_text(a.c_str(), wcslen(a.c_str()));

  }

}

void timer_esp_entity() {
  
  for (auto item = pickup_item_cache.begin(); item != pickup_item_cache.end();) {
    const float time_delta = item->time - global_vars->curtime;
    if (time_delta < 0) {
      item = pickup_item_cache.erase(item);
      continue;
    }
    
    Vec3 screen;
    if (!overlay_projection::world_to_screen(item->location, &screen)) {
      ++item;
      continue;
    }

    std::wstring time_delta_str = std::to_wstring(time_delta);

    time_delta_str.resize(4);
    
    surface->draw_set_text_color(255, 255, 255, 255);
    surface->draw_set_text_pos(screen.x - (surface->get_string_width(esp_entity_font, time_delta_str.c_str())*0.5), screen.y);
    surface->draw_print_text(time_delta_str.c_str(), wcslen(time_delta_str.c_str()));
    ++item;
  }

}

void esp_entity(unsigned int i, Entity* entity) {
  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr) return;

  Vec3 location = entity->get_origin();
  Vec3 screen;
  if (!overlay_projection::begin_frame(overlay_projection::screen_space_t::surface)) return;
  if (!overlay_projection::world_to_screen(location, &screen)) return;

  box_esp_entity(screen, entity, localplayer);
  health_bar_esp_entity(screen, entity, localplayer);
  name_esp_entity(screen, entity, localplayer);
  timer_esp_entity();
}
