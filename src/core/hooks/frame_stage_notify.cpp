/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/frame_stage_notify.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/steam_friends.hpp"

#include <string>
#include <utility>

#include "games/tf2/sdk/entities/player.hpp"

#include "core/entity_cache.hpp"
#include "core/commands.hpp"
#include "core/detach.hpp"
#include "core/identify/identify.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/player_manager.hpp"

#include "features/menu/config.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/combat/aimbot/resolver.hpp"
#include "features/combat/backtrack/backtrack.hpp"
#include "features/movement/local_prediction/move_sim.hpp"
#include "features/automation/navbot/navbot_controller.hpp"
#include "features/visuals/thirdperson.hpp"
#include "features/visuals/glow/player_model_glow.hpp"
#include "features/visuals/groups/visual_groups.hpp"

#include "core/print.hpp"

enum ClientFrameStage {
  FRAME_UNDEFINED = -1,
  FRAME_START,
  FRAME_NET_UPDATE_START,
  FRAME_NET_UPDATE_POSTDATAUPDATE_START,
  FRAME_NET_UPDATE_POSTDATAUPDATE_END,
  FRAME_NET_UPDATE_END,
  FRAME_RENDER_START,
  FRAME_RENDER_END
};

void (*frame_stage_notify_original)(void*, ClientFrameStage);

static float last_time = 0.0;
static unsigned int a = 0;

namespace
{

std::string last_match_exec_level{};

void run_match_exec_on_level_change()
{
  if (engine == nullptr || !engine->is_in_game()) {
    last_match_exec_level.clear();
    return;
  }

  const char* level_name = engine->get_level_name();
  if (level_name == nullptr || level_name[0] == '\0') {
    return;
  }

  if (last_match_exec_level == level_name) {
    return;
  }

  last_match_exec_level = level_name;
  cathook::core::execute_cfg_file("cat_matchexec", "cat_matchexec");
}

} // namespace

void frame_stage_notify_hook(void* me, ClientFrameStage current_stage) {
  if (cathook::core::is_detach_pending()) {
    thirdperson::end_render_angles();
    cathook::core::service_detach_request();
    return;
  }

  frame_stage_notify_original(me, current_stage);

  if (current_stage == FRAME_RENDER_START) {
    thirdperson::begin_render_angles();
  }

  // Init start time
  if (last_time == 0.0) {
    last_time = global_vars->curtime;
  }

  switch (current_stage) {
  case FRAME_NET_UPDATE_START:
    {
      
      if (global_vars->curtime - last_time >= 1) {
        friend_cache.clear();
      }
      
      entity_cache.clear();
      entity_cache_clear_snapshot();
      resolver::update_pending_shots();

      break;
    }

  case FRAME_NET_UPDATE_END:
    {
      entity_cache_snapshot snapshot{};

      for (unsigned int i = 1; i <= entity_list->get_max_entities(); ++i) {
	Entity* entity = entity_list->entity_from_index(i);
	if (entity == nullptr) continue;

        if (entity->is_network_class("CTFPumpkinBomb")) {
          entity_cache[class_id::PUMPKIN].push_back(entity);
        }

	switch (entity->get_class_id()) {
	case class_id::PLAYER:
	  {
            auto* player = static_cast<Player*>(entity);
            if (!player->is_dormant() && player->is_alive()) {
	      entity_cache[class_id::PLAYER].push_back(entity);
              snapshot.players.push_back({
                .player = player,
                .entity = entity,
                .index = player->get_index(),
                .simulation_time = player->get_simulation_time(),
                .origin = player->get_origin(),
                .velocity = player->get_velocity(),
                .team = player->get_team(),
                .player_class = static_cast<int>(player->get_tf_class()),
                .alive = true,
                .dormant = false,
                .friendly = player->is_friend(),
                .ignored = player->is_ignored()
              });
              local_prediction_record_entity(entity);
              resolver::record_player(player);
              backtrack::record_player(player);
            }
	    
	    if (steam_friends != nullptr && config.debug.disable_friend_checks == false && global_vars->curtime - last_time >= 1) {
	      player_info pinfo;
	      if (engine->get_player_info(entity->get_index(), &pinfo) && pinfo.friends_id != 0) { 
		friend_cache_store(pinfo.friends_id, steam_friends->is_friend(pinfo.friends_id));
	      }
	    }
	    
	    break;
	  }

	case class_id::AMMO_OR_HEALTH_PACK:
	  {
	    if (entity->get_pickup_type() == pickup_type::AMMOPACK)
	      entity_cache[class_id::AMMO].push_back(entity);
	    else if (entity->get_pickup_type() == pickup_type::MEDKIT)
	      entity_cache[class_id::HEALTH_PACK].push_back(entity);

	    break;
	  }

	case class_id::CAPTURE_FLAG:
	  entity_cache[class_id::CAPTURE_FLAG].push_back(entity); break;

	case class_id::OBJECTIVE_RESOURCE:
	  entity_cache[class_id::OBJECTIVE_RESOURCE].push_back(entity); break;

	case class_id::SENTRY:
	case class_id::OBJECT_CART_DISPENSER:
	case class_id::DISPENSER:
	case class_id::TELEPORTER:
	  entity_cache[entity->get_class_id()].push_back(entity); break;

	case class_id::SNIPER_DOT:
	  entity_cache[class_id::SNIPER_DOT].push_back(entity); break;

        case class_id::ROCKET:
        case class_id::PILL_OR_STICKY:
        case class_id::FLARE:
        case class_id::ARROW:
        case class_id::CROSSBOW_BOLT:
          if (entity->get_class_id() == class_id::PILL_OR_STICKY) {
            entity_cache[class_id::PILL_OR_STICKY].push_back(entity);
          }
          local_prediction_record_entity(entity); break;

	  
	}

      }

      snapshot.entities = entity_cache;
      entity_cache_publish_snapshot(std::move(snapshot));

      if (global_vars->curtime - last_time >= 1) {
	last_time = global_vars->curtime;
      }

      visual_groups::store(entity_list->get_localplayer());
      player_model_glow::store();
      
      break;
    }
  }

  if (current_stage == FRAME_RENDER_END) {
    thirdperson::end_render_angles();
  }

  if (current_stage == FRAME_NET_UPDATE_END) {
    run_match_exec_on_level_change();
    cat_ipc::client::tick();
    cathook::core::identify::tick();
    cathook::core::players::tick();
    automation::controller().on_frame_stage_notify();
    navbot::controller().on_frame_stage_notify();
  }
}
