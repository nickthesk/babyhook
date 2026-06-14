/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/shared/sigs.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef SHARED_SIGS_HPP
#define SHARED_SIGS_HPP

namespace sigs
{

constexpr const char* input =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 90 ? ? ? ? 48 8D 15 ? ? ? ? 84 C0";
constexpr const char* move_helper =
  "48 8D 05 ? ? ? ? 48 89 85 ? ? ? ? 74 ? 48 8B 38";
constexpr const char* client_state =
  "48 8D 05 ? ? ? ? 4C 8B 40";
constexpr const char* client_mode_shared =
  "48 8D 05 ? ? ? ? 40 0F B6 F6 48 8B 38 48 8B 07 FF 60 58";

constexpr const char* in_cond =
  "55 83 FE ? 48 89 E5 41 54 41 89 F4";
constexpr const char* load_white_list =
  "55 48 89 E5 41 55 41 54 49 89 FC 48 83 EC ? 48 8B 07 FF 50";
constexpr const char* cl_move =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC 78 83 3D ? ? ? ? 01 F3 0F 11 85 ? ? ? ? 0F 8E ? ? ? ? 41 89 FE E8 ? ? ? ? 84 C0 89 C3 0F 84 ? ? ? ? 4C 8B 3D ? ? ? ? 31";
constexpr const char* cl_read_packets =
  "55 31 C0 48 89 E5 41 57 41 56 41 55 41 89 FD 41 54 53 48 83 EC ? 48 8B 1D ? ? ? ? 48 C7 45 ? ? ? ? ?";
constexpr const char* cl_process_packet_entities =
  "55 48 89 E5 41 55 49 89 F5 41 54 0F B6 46 30 49 89 FC 84 C0 75 ? 48 8D 05 ? ? ? ? BE 06 00 00 00 48 8B 38 48 8B 07 FF 50 18 48 8D 05 ? ? ? ? 48 8B 38 48 85 FF 74 ? 48 8B 07 FF 50 40 48 8D 05 ? ? ? ? 48 83 38 00 74 ? 41 83 BC 24 4C ? ? ? ?";
constexpr const char* host_should_run =
  "48 8B 15 ? ? ? ? B8 01 00 00 00 8B 72 58 85 F6 74 ? 48 8B 0D ? ? ? ? 8B 15";
constexpr const char* prediction_run_simulation =
  "55 31 C0 48 89 E5 41 57 41 56 49 89 FE 41 55 41 89 F5 41 54 49 89 D4 53 48 89 CB 48 83 EC ? 4C 8B 3D ? ? ? ? F3 0F 11 45 ?";
constexpr const char* should_draw_local_player =
  "55 48 89 E5 41 54 48 83 EC ? 48 8D 05 ? ? ? ? 48 8B 38 48 85 FF 74 ? 48 8B 07 FF 50";
constexpr const char* should_draw_this_player =
  "55 48 89 E5 41 54 53 E8 ? ? ? ? 84 C0 75";
constexpr const char* draw_view_models =
  "55 31 C0 48 89 E5 41 57 41 56 41 55 41 89 D5 41 54 49 89 FC 53 48 89 F3 48 81 EC";
constexpr const char* attribute_hook_value_float =
  "55 31 C0 48 89 E5 41 57 41 56 41 55 49 89 F5 41 54 49 89 FC 53 89 CB";
constexpr const char* intro_menu_on_tick =
  "55 48 89 E5 41 55 41 54 49 89 FC 48 8B BF ? ? ? ? 48 8B 07 FF 90 ? ? ? ? 84 C0";
constexpr const char* class_menu_show_panel =
  "55 48 89 E5 41 55 41 54 49 89 FC 53 89 F3 40 0F B6 F6 48 83 EC ? E8 ? ? ? ? 84 DB 48 8D 1D";
constexpr const char* team_menu_show_panel =
  "55 48 89 E5 41 56 41 55 41 54 49 89 FC 53 48 83 EC ? 40 84 F6 0F 85";
constexpr const char* map_info_menu_show_panel =
  "55 48 8D 15 ? ? ? ? 48 89 E5 41 54 49 89 FC 53 48 8B 07 89 F3";
constexpr const char* text_window_show_panel =
  "55 48 8D 15 ? ? ? ? 48 89 E5 41 54 41 89 F4 53 48 8B 07";
constexpr const char* key_values_constructor =
  "55 66 0F EF C0 48 89 E5 53 48 89 FB 48 89 F7 48 83 EC ? BE ? ? ? ? C7 03 FF FF FF FF";
constexpr const char* key_values_set_int =
  "55 48 89 E5 53 89 D3 BA ? ? ? ? 48 83 EC ? E8 ? ? ? ? 48 85 C0 74 ? 89 58";
constexpr const char* key_values_load_from_buffer =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 81 EC ? ? ? ? 48 85 D2 48 89 BD";
constexpr const char* random_seed =
  "48 8D 05 ? ? ? ? BA ? ? ? ? 89 10";
constexpr const char* input_apply_mouse =
  "55 66 0F EF D2 48 89 E5 41 55 41 54 49 89 FC 53 48 83 EC 28";
constexpr const char* item_schema_lookup_map =
  "48 8B 05 ? ? ? ? 55 48 89 E5 41 55 41 54 48 85 C0 74 ? 4C 8D 60 ?";
constexpr const char* item_definition_lookup =
  "55 48 89 E5 41 55 41 54 49 89 FC 53 48 83 EC ? 8B 87 ? ? ? ? 85 C0 0F 84 ? ? ? ? 41 89 F1";
constexpr const char* setup_bones =
  "55 48 89 E5 41 57 41 56 41 55 41 54 41 89 CC 53 48 89 FB 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ?";
constexpr const char* tf_weapon_base_gun_get_bullet_spread =
  "55 31 D2 48 89 FE B9 ? ? ? ? 48 89 E5 41 54 53 48 89 FB 48 83 EC ? 48 63 87 ? ? ? ? 48 C1 E0 ?";
constexpr const char* ctf_weapon_base_calc_is_attack_critical =
  "55 48 89 E5 41 57 41 56 41 55 41 54 49 89 FC 53 48 83 EC ? E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 8D 15 ? ? ? ? 31 C9 48 89 C7 48 8D 35 ? ? ? ? E8 ? ? ? ? 48 85 C0 49 89 C5 0F 84 ? ? ? ? 48 8B 00 4C 89 EF FF 90 ? ? ? ? 84 C0";
constexpr const char* ctf_weapon_base_melee_calc_is_attack_critical =
  "55 48 89 E5 41 57 41 56 41 55 41 54 49 89 FC 53 48 83 EC ? E8 ? ? ? ? 48 85 C0 74 ? 48 89 C3 48 8B 00 48 89 DF FF 90 ? ? ? ? 84 C0 74 ? 49 8B 04 24 31 D2 31 F6 4C 89 E7 FF 90 ? ? ? ? 84 C0";
constexpr const char* ctf_player_anim_state_store =
  "E8 ? ? ? ? 48 8B 7D ? 49 89 84 24 ? ? ? ? 4C 89 E6 E8 ? ? ? ? 49 8D B4 24 ? ? ? ?";
constexpr const char* base_animating_auto_allow_bone_access =
  "44 0F B6 C2 40 0F B6 FE BA 01 00 00 00 44 89 C6 E9 ? ? ? ?";
constexpr const char* base_animating_auto_allow_bone_access_on_delete =
  "55 BF 01 00 00 00 48 89 E5 E8 ? ? ? ? 5D C3";
constexpr const char* inspect_target_check =
  "55 48 89 E5 41 55 41 54 53 48 81 EC ? ? ? ? 48 85 F6 74 ? 48 8D 05 ? ? ? ?";
constexpr const char* tf_projectile_sticky_arm_time =
  "55 48 89 E5 41 54 49 89 FC 48 83 EC ? 48 8B 05 ? ? ? ? 8B 97 ? ? ? ? F3 0F 10 40 ? 48 8D 05 ? ? ? ?";
constexpr const char* navbot_server_compute_blocked_areas =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 31 DB 48 81 EC ? ? ? ? 4C 8D 25 ? ? ? ? 45 8B 4C 24 10 45 85 C9 7E ? 66 0F 1F 84 00 00 00 00 00 49 8B 04 24 BE FE FF FF FF 48 8B 3C D8 48 83 C3 01 E8 ? ? ? ? 41 39 5C 24 10";

constexpr const char* tf_inventory_manager_initializer =
  "55 48 8D 3D ? ? ? ? 48 89 E5 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8D 35 ? ? ? ? 48 8D 3D ? ? ? ? E8 ? ? ? ? 48 8D 15 ? ? ? ?";

constexpr const char* get_party_client =
  "48 8D 05 ? ? ? ? C3 0F 1F 84 00 00 00 00 00 48 8B 05 ? ? ? ? C3";
constexpr int get_party_client_offset = 16;
constexpr const char* get_matchmaking_client =
  "48 8D 05 ? ? ? ? C3 0F 1F 84 00 00 00 00 00 48 8B 05 ? ? ? ? C3 0F 1F 84 00 00 00 00 00 48 8D 05 ? ? ? ? 48 8B 00 48 85 C0 74 ? 55";
constexpr const char* tf_gc_client_system_ping_think =
  "55 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 48 8D 3D ? ? ? ? 53 48 81 EC ? ? ? ?";
constexpr const char* tf_gc_client_system_so_event =
  "55 48 89 E5 41 57 41 56 49 89 FE 48 89 F7 41 55 41 89 D5 41 54 49 89 F4 53 48 83 EC ? 48 8B 06 FF 50 10 3D D4 07 00 00 0F 84 ? ? ? ? 49 8B 04 24 4C 89 E7 FF 50 10 83 F8 2A 74 ? 49 8B 04 24 4C 89 E7 FF 50 10 3D D8 07 00 00";
constexpr const char* tf_gc_client_system_request_accept_match_invite =
  "48 83 BF ? ? ? ? 00 74 ? C3 0F 1F 44 00 00 55 48 89 E5 41 57 49 89 F7 41 56 41 55 41 54 49 89 FC BF ? ? ? ? 53 48 83 EC ? E8 ? ? ? ? 49 89 C6 E8 ? ? ? ? 48 89 C7 E8 ? ? ? ? 48 8D 70 ?";

constexpr const char* load_saved_casual_criteria =
  "48 83 7F 30 00 C6 87 10 03 00 00 01 74 ? 80 7F 40 00 74 ? C6 87 30 03 00 00 01 48 8D 35 ? ? ? ? 48 81 C7 B0 01 00 00 E9 ? ? ? ?";
constexpr const char* set_casual_criteria_map_selected =
  "55 48 89 E5 41 56 41 55 41 89 D5 41 54 41 89 F4 53 48 89 FB 48 83 EC 50 48 8B 07 FF 50 10 48 8B 70 58 48 85 F6";
constexpr const char* is_in_queue_for_match_group =
  "55 48 89 E5 41 54 49 89 FC 89 F7 53 89 F3 E8 ? ? ? ? 83 FB FF 41 89 C0 0F 94 C0 41 83 F0 01 41 08 C0 75 ? 41 8B 54 24 58 85 D2";
constexpr const char* is_in_standby_queue =
  "0F B6 47 68 C3";
constexpr const char* abandon_current_match =
  "55 31 C0 48 89 E5 41 55 41 54 4C 8D 65 ? 53 48 89 FB 48 8D 3D ? ? ? ? 48 83 EC 38 E8 ? ? ? ? 4C 89 E7 BE 91 18 00 00 E8 ? ? ? ? 48 8B 45 ? 48 89 DF 83 48 10 01 C6 40 28 01 4C 8B 6D ? E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 89 DF E8 ? ? ? ? 48 8D 0D ? ? ? ? 48 89 C7 48 8B 00 48 8B 50 10 48 39 CA";
constexpr const char* request_queue_for_match =
  "55 48 89 E5 41 57 41 56 49 89 FE 89 F7 41 55 41 54 41 89 F4 53 48 81 EC 88 00 00 00 E8 ? ? ? ? 41 83 FC FF 0F 94 C3 3C 01 75 ? 84 DB 75 ? 49 63 C4 41 80 BC 06 1E 03 00 00 00 75 ?";
constexpr const char* request_leave_for_match =
  "55 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 53 48 63 DE 48 83 EC ? 89 DE E8 ? ? ? ? 84 C0 75 ? 48 83 C4 ? 5B 41 5C 41 5D 41 5E 41 5F 5D";
constexpr const char* request_queue_for_standby =
  "48 83 7F 30 00 0F 84 ? ? ? ? 55 48 89 E5 41 57 41 56 41 55 41 54 49 89 FC 53 48 83 EC 38 80 BF 12 03 00 00 00 74 ?";
constexpr const char* request_leave_standby =
  "80 7F 68 00 0F 84 ? ? ? ? 55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB 48 83 EC 38 80 BF 13 03 00 00 00 74 ?";
constexpr const char* report_player_account =
  "55 48 89 F8 48 89 E5 48 C1 E8 ? 41 57 41 56 41 55 41 54 53 48 83 EC ?";
constexpr const char* allow_secure_servers_flag_ref =
  "48 8D 05 ? ? ? ? 4C 89 E7 C6 00 00 4C 8B 65 ? C9 E9 ? ? ? ?";
constexpr const char* host_is_secure_server_allowed =
  "55 48 89 E5 E8 ? ? ? ? 48 8D 35 ? ? ? ? 48 89 C7 48 8B 00 FF 50 50 85 C0 74 ? 31 C0 5D C6 05 ? ? ? ? 00 C3 0F 1F 84 00 00 00 00 00 E8 ? ? ? ? 48 8D 35 ? ? ? ? 48 89 C7 48 8B 00 FF 50 50 85 C0 75 ? 0F B6 05 ? ? ? ? 5D C3";
constexpr const char* launcher_source_lock =
  "55 48 89 E5 41 55 41 54 4C 8D AD ? ? ? ? 48 81 EC ? ? ? ? E8 ? ? ? ?";
constexpr const char* startup_video =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 81 EC ? ? ? ? 48 89 BD ? ? ? ? E8 ? ? ? ? 84 C0 0F 85 ? ? ? ? 41 89 C5";
constexpr const char* video_mode_setup_startup_graphic =
  "55 31 C0 48 89 E5 41 57 41 56 4C 8D B5 ? ? ? ? 41 55 41 54 4C 8D A5 ? ? ? ? 53 48 89 FB 48 8D 3D ? ? ? ? 48 81 EC ? ? ? ? E8 ? ? ? ? 31 D2 BE ? ? ? ? 4C 89 F7";
constexpr const char* client_file_system =
  "31 F6 4C 89 EF FF 13 48 83 3D ? ? ? ? 00 48 89 05 ? ? ? ? 0F 85";
constexpr const char* base_animating_dispatch_anim_events =
  "48 85 F6 0F 84 ? ? ? ? 55 48 89 E5 41 57 41 56 41 55 41 54 49 89 FC 53 48 83 EC ? 83 BF ? ? ? ? ? 0F 84 ? ? ? ? 66 83 BF ? ? ? ? ?";
constexpr const char* cl_decay_lights =
  "55 48 8D 3D ? ? ? ? 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC ? E8 ? ? ? ? 66 0F EF C9";
constexpr const char* engine_fps_max_min_clamp =
  "0F 2F 05 ? ? ? ? 0F 83 ? ? ? ? 48 8D 3D ? ? ? ? 31 C0 F3 0F 11 4D";
constexpr const char* engine_frame_busy_wait =
  "B8 D1 07 00 00 0F 1F 00 F3 90 F3 90 F3 90 F3 90 83 E8 01 75 F3";
constexpr const char* engine_client_process_voice_data =
  "55 BA 00 10 00 00 48 89 E5 41 57 41 56 4C 8D BD ? ? ? ? 41 55 41 54 49 89 FC 53 48 89 F3 48 81 EC B8 10 00 00 48 63 4E 30 48 89 BD ? ? ? ? 48 8D 7E 40 4C 89 FE E8 ? ? ? ? 44 8B 6B 28";
constexpr const char* mod_load_lighting =
  "55 48 89 E5 41 54 4C 8B 25 ? ? ? ? 53 48 63 37 85 F6 75 ? 49 C7 84 24 ? ? ? ? ? ? ? ?";
constexpr const char* mod_load_worldlights =
  "48 8B 0D ? ? ? ? 48 C7 81 ? ? ? ? ? ? ? ? 48 63 07 85 C0 0F 84 ? ? ? ?";
constexpr const char* mod_load_texinfo_material_branch =
  "41 8B 44 24 10 66 89 43 48 31 C0 85 D2 66 89 43 4A 0F 85 ? ? ? ? 48 8D 05 ? ? ? ? 48 8B 38 48 89 7B 50";
constexpr const char* sprite_load_model =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 F3 48 81 EC ? ? ? ? 4C 8D 35 ? ? ? ?";
constexpr const char* overlay_mgr_load_overlays =
  "55 BE ? ? ? ? 48 89 E5 41 57 41 56 4C 8D B5 ? ? ? ? 41 55 41 54 53 48 81 EC ? ? ? ? 48 89 BD ? ? ? ? 4C 89 F7";
constexpr const char* shadow_mgr_render_shadows =
  "55 31 C0 48 89 E5 41 57 41 56 41 55 49 89 F5 41 54 49 89 FC 53 48 83 EC 28 48 8B 1D ? ? ? ? 48 C7 45 B0 00 00 00 00";
constexpr const char* static_prop_mgr_draw_static_props =
  "55 31 C0 48 89 E5 41 57 41 56 41 89 D6 41 55 49 89 F5 41 54 49 89 FC 53 89 CB 48 83 EC 38 4C 8B 3D ? ? ? ? 44 89 45 AC";
constexpr const char* engine_sound_emit_sound_internal =
  "55 48 89 E5 41 57 41 89 D7 41 56 41 55 41 54 49 89 F4 53 48 81 EC B8 00 00 00 8B 45 40 89 8D 30 FF FF FF 48 8B 1D ? ? ? ?";
constexpr const char* s_precache_sound =
  "55 48 89 E5 41 55 49 89 FD 41 54 53 48 83 EC 28 48 8B 1D ? ? ? ? 48 C7 45 C8 00 00 00 00 4C 8B 23 4D 85 E4 0F 84 ? ? ? ?";
constexpr const char* svc_bspdecal_process =
  "55 48 89 E5 53 48 89 F3 48 83 EC 08 8B 56 38 85 D2 75 ? 48 8D 05 ? ? ? ? 48 8B 08 48 85 C9 0F 84 ? ? ? ?";
constexpr const char* r_draw_decals_all_0 =
  "55 48 89 E5 41 57 49 89 FF 41 56 48 8D 85 B8 F3 FF FF 41 55 41 54 53 48 89 CB 48 81 EC E8 0F 00 00 48 89 85 00 F0 FF FF 48 89 45 C0 48 8B 05 ? ? ? ? 48 89 BD 08 F0 FF FF";
constexpr const char* r_draw_decals_all_1 =
  "55 48 89 E5 41 57 48 8D 85 B8 F3 FF FF 41 56 41 55 41 54 4C 63 E2 53 48 63 DE 48 81 EC 18 10 00 00 48 89 85 E0 EF FF FF 48 89 45 C0 48 8B 05 ? ? ? ? F3 0F 11 85 04 F0 FF FF";
constexpr const char* v_render_view =
  "55 31 C0 48 89 E5 41 56 41 55 41 54 53 48 83 EC 40 4C 8B 2D ? ? ? ? 48 C7 45 A0 00 00 00 00 49 8B 7D 18 48 85 FF 74 ? 48 83 EC 08 45 31 C0 31 C9 48 8D 05 ? ? ? ? 31 D2";
constexpr const char* material_system_begin_frame =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB 48 83 EC ? F3 0F 11 85 ? ? ? ?";
constexpr const char* material_system_swap_buffers =
  "55 31 C0 48 89 E5 41 56 41 55 41 54 53 48 89 FB 48 83 EC 20 4C 8B 25 ? ? ? ? 48 C7 45 C8 00 00 00 00 49 8B 7C 24 10 48 85 FF 74 50";
constexpr const char* material_system_texture_load_bits =
  "55 48 89 E5 41 57 41 56 49 89 FE 41 55 49 89 D5 31 D2 41 54 53 48 89 F3 48 83 EC 68 4C 8B 25 ? ? ? ? 48 C7 45 ? ? ? ? ? 49 8B 04 24 48 85 C0 74";
constexpr const char* material_system_get_scratch_vtf_texture =
  "48 8D 05 ? ? ? ? 55 48 89 E5 41 54 53 4C 8D 25 ? ? ? ? 48 8B 38 48 8B 07 FF 90 B8 04 00 00";
constexpr const char* material_system_handle_file_load_failed_texture =
  "55 41 B8 0C 00 00 00 B9 01 00 00 00 BA 20 00 00 00 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 49 89 F4";
constexpr const char* material_var_cleanup =
  "55 48 89 E5 41 54 49 89 FC 53 48 8B 77 50 48 85 F6 74 ? 80 7F 2A 00 74 ? 31 DB 0F 1F 44 00 00 48 8B 3C DE 48 83 C3 01 E8 ? ? ? ? 41 0F B6 44 24 2A 49 8B 74 24 50 39 D8";
constexpr const char* material_queued_call_drain =
  "48 8B 05 ? ? ? ? 55 48 89 E5 41 56 41 89 F6 41 55 49 89 FD 41 54 53 8B 40 58 85 C0 75 ? 49 8B 9D ? ? ? ? 48 85 DB 74 ? 0F 1F 44 00 00 4C 8B 63 08 49 8B 04 24";
constexpr const char* shaderapivk_apply_pending_transition_snapshot =
  "55 48 8D 15 ? ? ? ? 48 89 E5 53 48 89 FB 48 83 EC 08 48 8B 07 48 8B 80 ? ? ? ? 48 39 D0 75 ? 8B 8F ? ? ? ? 85 C9";
constexpr const char* particle_property_create =
  "55 48 89 E5 41 57 41 56 49 89 F6 41 55 41 54 53 48 89 FB 48 83 EC ? 48 8D 05 ? ? ? ? 89 55 ? 89 4D ? 66 0F D6 45 ?";
constexpr const char* particle_system_precache =
  "31 C0 48 85 FF 74 ? 55 48 89 E5 41 54 49 89 FC 48 83 EC ? 48 8B 3D ? ? ? ?";
constexpr const char* particle_effect_create_event =
  "55 41 89 C9 48 89 E5 41 57 41 56 41 55 4D 89 C5 41 54 49 89 FC 53 48 81 EC ? ? ? ?";
constexpr const char* view_render_render =
  "55 31 C0 48 89 E5 41 57 49 89 FF 41 56 41 55 41 54 53 48 81 EC ? ? ? ? 48 8B 1D ? ? ? ? 48 89 B5 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ?";
constexpr const char* replay_screenshot_render =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 81 EC ? ? ? ? 48 83 3F ?";
constexpr const char* client_youtube_system_init =
  "55 48 89 E5 53 48 89 FB 48 81 EC 28 01 00 00 E8 ? ? ? ? 48 8B 15 ? ? ? ? 48 B9 02 00 00 00 FF FF FF FF 48 89 83 ? ? ? ? 48 89 C7";
constexpr const char* client_replay_layoff_frame =
  "55 31 C0 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB 48 81 EC 88 01 00 00";
constexpr const char* client_update_steam_rich_presence =
  "55 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 53 48 81 EC ? ? ? ? 48 8B 1D ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 48 8B 3B 48 85 FF";
constexpr const char* client_achievement_mgr_post_init =
  "55 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 48 8D 3D ? ? ? ? 53 48 83 EC 28 E8 ? ? ? ? 84 C0";
constexpr const char* client_achievement_save_global_state =
  "55 31 C0 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB 48 83 EC 38 4C 8B 25 ? ? ? ? 89 75 AC";
constexpr const char* client_tf_steam_stats_upload_stats =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 81 EC 28 01 00 00 4C 8D 3D ? ? ? ? 48 89 BD ? ? ? ? 49 8B 07 48 83 78 30 00";
constexpr const char* client_achievement_progress_event =
  "55 48 8D 15 ? ? ? ? 48 8D 35 ? ? ? ? 48 89 E5 41 56 41 55 41 54 49 89 FC 48 83 EC 08 48 8B 07 FF 50 48 48 8D 35 ? ? ? ?";
constexpr const char* client_abuse_report_notification =
  "55 48 89 E5 41 56 66 41 0F 7E C6 41 55 41 89 F5 41 54 53 48 89 FB 48 8D 3D ? ? ? ? E8 ? ? ? ? BF 48 10 00 00";
constexpr const char* client_abuse_incident_poll =
  "55 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 53 48 81 EC 18 01 00 00 44 8B 5F 18 45 85 DB 0F 8E ? ? ? ?";
constexpr const char* replay_ui_nullcheck_0 =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 50 38 84 C0 74 ? 48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 90 90 01 00 00";
constexpr const char* replay_ui_nullcheck_1 =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 50 38 84 C0 74 ? 45 84 E4 74 ?";
constexpr const char* replay_ui_nullcheck_2 =
  "4C 8D 35 ? ? ? ? 48 89 C3 49 8B 3E 48 8B 07 FF 50 38 48 85 DB";
constexpr const char* replay_ui_nullcheck_3 =
  "49 8B 3E 48 8B 07 FF 50 38 84 C0 0F 85 ? ? ? ? E8";
constexpr const char* replay_ui_nullcheck_4 =
  "4C 8D 35 ? ? ? ? 49 8B 3E 48 8B 07 FF 50 38 E9 ? ? ? ?";
constexpr const char* replay_ui_nullcheck_5 =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 50 30 84 C0 0F 85 ? ? ? ? 48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 90 D8 00 00 00";
constexpr const char* replay_ui_nullcheck_6 =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 90 D8 00 00 00 48 8D 05 ? ? ? ? 48 8D 35";
constexpr const char* replay_ui_nullcheck_7 =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 50 38 84 C0 0F 84 ? ? ? ? 4C 8D 3D";
constexpr const char* replay_ui_nullcheck_8 =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 90 D0 00 00 00 E9 ? ? ? ?";
constexpr const char* client_objective_flag_countdown_update =
  "49 8B BC 24 ? ? ? ? 48 8B 07 FF 90 ? ? ? ? 84 C0 0F 85 ? ? ? ? 49 8B 06 48 85 C0 74 ?";
constexpr const char* econ_item_view_get_item_definition_index =
  "48 8B 47 08 48 85 C0 74 ? 8B 40 20 C3 0F 1F 00 48 8B 07 8B 80 BC 00 00 00 C3";
constexpr const char* character_info_open =
  "55 31 FF 48 89 E5 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 89 C7 48 05 30 03 00 00 48 85 FF 48 0F 45 F8 48 8B 07 48 8B 00 48 39 D0 75 ? 5D 48 81 EF 30 03 00 00 31 D2 31 F6";
constexpr const char* character_info_open_direct =
  "55 48 89 E5 41 55 41 54 53 48 89 FB 48 83 EC 08 48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 90 D0 00 00 00 84 C0 75";
constexpr const char* character_info_open_backpack =
  "55 31 FF 48 89 E5 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 89 C7 48 05 30 03 00 00 48 85 FF 48 0F 45 F8 48 8B 07 48 8B 00 48 39 D0 75 ? 5D 48 81 EF 30 03 00 00 31 D2 BE 01 00 00 00";
constexpr const char* character_info_open_crafting =
  "55 31 FF 48 89 E5 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 89 C7 48 05 30 03 00 00 48 85 FF 48 0F 45 F8 48 8B 07 48 8B 00 48 39 D0 75 ? 5D 48 81 EF 30 03 00 00 31 D2 BE 02 00 00 00";
constexpr const char* character_info_open_armory =
  "55 31 FF 48 89 E5 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 89 C7 48 05 30 03 00 00 48 85 FF 48 0F 45 F8 48 8B 07 48 8B 00 48 39 D0 75 ? 5D 48 81 EF 30 03 00 00 31 D2 BE 03 00 00 00";
constexpr const char* studio_render_draw_model_wrapper =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 81 EC 18 03 00 00 4C 8D 25 ? ? ? ? 48 89 95";
constexpr const char* studio_render_blend_bones =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC 48 44 8B B6 98 00 00 00";
constexpr const char* studio_render_matrix_multiply =
  "F3 0F 7E 4F 10 F3 0F 7E 3E F3 0F 7E 46 10 0F 16 4F 18 F3 0F 7E 66 20 0F 16 7E 08 44 0F 28 D9";
constexpr const char* client_econ_panel_flex_primary =
  "80 BF 08 1C 00 00 00 74 07 31 C0 C3 0F 1F 40 00 55 48 89 E5 41 57 41 56 4C 8D B7 98 15 00 00 41 55 41 54 49 89 FC";
constexpr const char* client_econ_panel_flex_attachments =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 81 EC 08 19 00 00 8B 87 30 1C 00 00 48 89 B5";
constexpr const char* client_ragdoll_lru_update =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB 48 83 EC 48 4C 8B 25 ? ? ? ? 48 C7 45 C8 00";
constexpr const char* client_particle_mgr_simulate_undrawn =
  "55 31 C0 48 89 E5 41 56 41 55 41 54 53 48 89 FB 48 83 EC 20 4C 8B 25 ? ? ? ? 48 C7 45 C8 00 00 00 00 49 8B 7C 24 18 48 85 FF 74 ? 48 83 EC 08 45 31 C0 31 C9 48 8D 05 ? ? ? ? 31 D2 50 48 8D 05 ? ? ? ? 50 48 8D 05 ? ? ? ? 50 48 8D 05 ? ? ? ? 50 48 8D 75 C8 31 C0 68 CA 05 00 00";
constexpr const char* client_temp_ents_update =
  "55 31 C0 48 89 E5 41 57 41 56 41 55 41 54 49 89 FC 53 48 83 EC 38 48 8B 1D ? ? ? ? 48 C7 45 B8 00 00 00 00 48 8B 7B 18 48 85 FF 74 ? 48 83 EC 08 45 31 C0 31 C9 48 8D 05 ? ? ? ? 31 D2 50 48 8D 05 ? ? ? ? 50 48 8D 05 ? ? ? ? 50 48 8D 05 ? ? ? ? 50 48 8D 75 B8 31 C0 68 34 09 00 00";
constexpr const char* client_rope_manager_draw_render_cache =
  "55 48 89 E5 41 57 49 89 FF 41 56 41 55 41 54 53 89 F3 48 81 EC A8 04 00 00 48 8B 05 ? ? ? ?";
constexpr const char* client_init_caption_dictionary =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 81 EC B8 11 00 00 66 83 BF 2C 03";
constexpr const char* client_parse_particle_effects_map =
  "55 48 89 E5 41 56 41 55 49 89 FD BF 48 00 00 00 41 54 53 48 83 EC 10 E8 ? ? ? ? 48 8D 35 ? ? ? ? 48 89 C7 49 89 C4 E8 ? ? ? ? 48 8D 05 ? ? ? ? 48 8D 0D ? ? ? ? 4C 89 E7";

} // namespace sigs

#endif
