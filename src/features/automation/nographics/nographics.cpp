/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/automation/nographics/nographics.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "features/automation/nographics/nographics.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/memory/byte_patch.hpp"
#include "core/print.hpp"
#include "core/shared/sigs.hpp"

#include "features/menu/config.hpp"

#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/file_system.hpp"
#include "games/tf2/sdk/interfaces/material_system.hpp"
#include "games/tf2/sdk/interfaces/mdl_cache.hpp"

#include "funchook/funchook.h"
#include "libsigscan/libsigscan.h"

#if defined(__linux__)
#include <dlfcn.h>
#endif

bool write_to_table(void** vtable, int index, void* func);
void* get_interface(const char* lib_path, const char* version);

namespace nographics
{

namespace
{

constexpr int file_system_find_first_index = 27;
constexpr int file_system_find_next_index = 28;
constexpr int file_system_async_read_multiple_index = 37;
constexpr int file_system_open_ex_index = 69;
constexpr int file_system_read_file_ex_index = 71;
constexpr int file_system_add_files_to_cache_index = 103;
constexpr int base_file_system_open_index = 2;
constexpr int base_file_system_precache_index = 9;
constexpr int base_file_system_file_exists_index = 10;
constexpr int base_file_system_read_file_index = 14;
constexpr std::uintptr_t base_file_system_vptr_offset = sizeof(void*);
constexpr int material_system_create_render_target_texture_index = 84;
constexpr int material_system_create_named_render_target_texture_ex_index = 85;
constexpr int material_system_create_named_render_target_texture_index = 86;
constexpr int material_system_create_named_render_target_texture_ex2_index = 87;
constexpr int studio_render_draw_model_index = 29;
constexpr int studio_render_draw_model_static_prop_index = 30;
constexpr int studio_render_draw_static_prop_decals_index = 31;
constexpr int studio_render_draw_static_prop_shadows_index = 32;
constexpr int studio_render_add_decal_index = 36;
constexpr int studio_render_add_shadow_index = 39;
constexpr int studio_render_draw_model_array_index = 46;
constexpr int mdl_cache_touch_all_data_index = 17;
constexpr int mdl_cache_touch_all_data_extra_index = 45;
constexpr const char* client_module_name = "tf/bin/linux64/client.so";
constexpr int engine_frame_busy_wait_usleep_delta = 0x1A;
constexpr int engine_frame_usleep_call_offset = 0x56;
constexpr int relative_jump_size = 5;
constexpr int fs_async_err_fileopen = -1;

bool sleep_pacing_patch_enabled()
{
  const char* value = std::getenv("CAT_NOGRAPHICS_SLEEP_PACING");
  return value != nullptr && std::strcmp(value, "1") == 0;
}

using find_first_fn = const char* (*)(void*, const char*, file_find_handle_t*);
using find_next_fn = const char* (*)(void*, file_find_handle_t);
using open_ex_fn = file_handle_t (*)(void*, const char*, const char*, unsigned int, const char*, char**);
using read_file_ex_fn = int (*)(void*, const char*, const char*, void**, bool, bool, int, int, void*);
using add_files_to_cache_fn = void (*)(void*, file_cache_handle_t, const char**, int, const char*);
using open_fn = file_handle_t (*)(void*, const char*, const char*, const char*);
using precache_fn = bool (*)(void*, const char*, const char*);
using file_exists_fn = bool (*)(void*, const char*, const char*);
using read_file_fn = bool (*)(void*, const char*, const char*, void*, int, int, void*);
using texture_load_bits_fn = void* (*)(void*, const char*, char**);
using get_scratch_vtf_texture_fn = void* (*)(void*);
using handle_file_load_failed_texture_fn = void* (*)(void*, void*);
using bone_setup_attachment_matrices_fn = std::int64_t (*)(void*, const void*, int, void*, void*, std::int64_t, int);
using create_render_target_texture_fn = Texture* (*)(void*, int, int, render_target_size_mode, image_format, material_render_target_depth);
using create_named_render_target_texture_ex_fn = Texture* (*)(void*, const char*, int, int, render_target_size_mode, image_format, material_render_target_depth, unsigned int, unsigned int);
using create_named_render_target_texture_fn = Texture* (*)(void*, const char*, int, int, render_target_size_mode, image_format, material_render_target_depth, bool, bool);
using studio_render_draw_model_fn = void (*)(void*, void*, const void*, void*, void*, void*, const void*, int);
using studio_render_draw_model_static_prop_fn = void (*)(void*, const void*, const void*, int);
using studio_render_draw_static_prop_decals_fn = void (*)(void*, const void*, const void*);
using studio_render_draw_static_prop_shadows_fn = void (*)(void*, const void*, const void*, int);
using studio_render_add_decal_fn = void (*)(void*, void*, void*, void*, const void*, const void*, void*, float, int, bool, int);
using studio_render_add_shadow_fn = void (*)(void*, void*, void*, void*, void*, void*);
using studio_render_draw_model_array_fn = void (*)(void*, const void*, int, void*, int, int);
using mdl_cache_touch_all_data_fn = void (*)(void*, unsigned short);

struct file_async_request;
using fs_async_callback_fn = void (*)(const file_async_request&, int, int);

struct file_async_request
{
  const char* filename;
  void* data;
  int offset;
  int bytes;
  fs_async_callback_fn callback;
  void* context;
  int priority;
  unsigned int flags;
  const char* path_id;
  void* specific_async_file;
  void* alloc_fn;
};

using async_read_multiple_fn = int (*)(void*, const file_async_request*, int, void*);

struct convar_override
{
  const char* name;
  int wanted_value;
  Convar* convar;
  int original_value;
  bool original_saved;
};

find_first_fn find_first_original = nullptr;
find_next_fn find_next_original = nullptr;
async_read_multiple_fn async_read_multiple_original = nullptr;
open_ex_fn open_ex_original = nullptr;
read_file_ex_fn read_file_ex_original = nullptr;
add_files_to_cache_fn add_files_to_cache_original = nullptr;
open_fn open_original = nullptr;
precache_fn precache_original = nullptr;
file_exists_fn file_exists_original = nullptr;
read_file_fn read_file_original = nullptr;
texture_load_bits_fn texture_load_bits_original = nullptr;
get_scratch_vtf_texture_fn get_scratch_vtf_texture = nullptr;
handle_file_load_failed_texture_fn handle_file_load_failed_texture = nullptr;
bone_setup_attachment_matrices_fn bone_setup_attachment_matrices_original = nullptr;
create_render_target_texture_fn create_render_target_texture_original = nullptr;
create_named_render_target_texture_ex_fn create_named_render_target_texture_ex_original = nullptr;
create_named_render_target_texture_fn create_named_render_target_texture_original = nullptr;
create_named_render_target_texture_ex_fn create_named_render_target_texture_ex2_original = nullptr;
studio_render_draw_model_fn studio_render_draw_model_original = nullptr;
studio_render_draw_model_static_prop_fn studio_render_draw_model_static_prop_original = nullptr;
studio_render_draw_static_prop_decals_fn studio_render_draw_static_prop_decals_original = nullptr;
studio_render_draw_static_prop_shadows_fn studio_render_draw_static_prop_shadows_original = nullptr;
studio_render_add_decal_fn studio_render_add_decal_original = nullptr;
studio_render_add_shadow_fn studio_render_add_shadow_original = nullptr;
studio_render_draw_model_array_fn studio_render_draw_model_array_original = nullptr;
mdl_cache_touch_all_data_fn mdl_cache_touch_all_data_original = nullptr;
mdl_cache_touch_all_data_fn mdl_cache_touch_all_data_extra_original = nullptr;

void** file_system_vtable = nullptr;
void** base_file_system_vtable = nullptr;
void** material_system_vtable = nullptr;
void* studio_render_interface = nullptr;
void** studio_render_vtable = nullptr;
void** mdl_cache_vtable = nullptr;
funchook_t* texture_load_bits_funchook = nullptr;
funchook_t* bone_setup_attachment_matrices_funchook = nullptr;
bool file_system_hooked = false;
bool material_system_render_target_hooked = false;
bool studio_render_hooked = false;
bool mdl_cache_touch_all_data_hooked = false;
bool texture_load_bits_hooked = false;
bool texture_load_bits_hook_failed = false;
bool bone_setup_attachment_matrices_hooked = false;
bool bone_setup_attachment_matrices_hook_failed = false;
bool material_stub_enabled = false;
bool render_patches_applied = false;
bool optional_render_patches_applied = false;
bool initialized = false;
bool render_patches_initialized = false;
bool render_patches_ready = false;
bool engine_render_patches_initialized = false;
bool materialsystem_render_patches_initialized = false;
bool client_textmode_cpu_patches_initialized = false;
std::atomic_bool startup_patch_running = false;
std::array<convar_override, 20> nographics_convar_overrides{ {
  { "mat_norendering", 0, nullptr, 0, false },
  { "mat_queue_mode", 0, nullptr, 0, false },
  { "engine_no_focus_sleep", 0, nullptr, 0, false },
  { "r_3dsky", 0, nullptr, 0, false },
  { "r_drawdetailprops", 0, nullptr, 0, false },
  { "r_drawflecks", 0, nullptr, 0, false },
  { "r_drawmodeldecals", 0, nullptr, 0, false },
  { "r_drawropes", 0, nullptr, 0, false },
  { "r_decals", 0, nullptr, 0, false },
  { "mp_decals", 0, nullptr, 0, false },
  { "cl_detaildist", 0, nullptr, 0, false },
  { "cl_detailfade", 0, nullptr, 0, false },
  { "cl_ejectbrass", 0, nullptr, 0, false },
  { "cl_show_splashes", 0, nullptr, 0, false },
  { "mat_disable_bloom", 1, nullptr, 0, false },
  { "mat_reducefillrate", 1, nullptr, 0, false },
  { "mat_picmip", 2, nullptr, 0, false },
  { "mat_specular", 0, nullptr, 0, false },
  { "mat_bumpmap", 0, nullptr, 0, false },
  { "mat_phong", 0, nullptr, 0, false },
} };

#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE
constexpr bool textmode_build = true;
#else
constexpr bool textmode_build = false;
#endif

bool module_is_loaded(const char* module_name)
{
  auto* bounds = sigscan_get_module_bounds(SIGSCAN_PID_SELF, module_name);
  if (bounds == nullptr)
  {
    return false;
  }

  sigscan_free_module_bounds(bounds);
  return true;
}

bool command_line_has_noshaderapi()
{
  std::ifstream cmdline{ "/proc/self/cmdline", std::ios::binary };
  if (!cmdline)
  {
    return false;
  }

  std::string argument{};
  for (char value{}; cmdline.get(value);)
  {
    if (value == '\0')
    {
      if (argument == "-noshaderapi")
      {
        return true;
      }
      argument.clear();
      continue;
    }
    argument.push_back(value);
  }

  return argument == "-noshaderapi";
}

bool empty_shader_api_is_active()
{
  if (!module_is_loaded("shaderapiempty.so"))
  {
    return false;
  }

  return !module_is_loaded("shaderapidx9.so") &&
         !module_is_loaded("shaderapivk.so") &&
         !module_is_loaded("togl.so");
}

bool is_shaderapivk_path(std::string_view library_path)
{
  const auto slash = library_path.find_last_of('/');
  const std::string_view name = slash == std::string_view::npos ? library_path : library_path.substr(slash + 1);
  return name == "shaderapivk.so" || name == "shaderapivk";
}

std::string_view library_basename(const char* library_path)
{
  if (library_path == nullptr)
  {
    return {};
  }

  const std::string_view path{ library_path };
  const auto slash = path.find_last_of('/');
  if (slash == std::string_view::npos)
  {
    return path;
  }

  return path.substr(slash + 1);
}

bool is_startup_patch_module(const char* library_path)
{
  const std::string_view name = library_basename(library_path);
  return name == "engine.so" ||
         name == "client.so" ||
         name == "materialsystem.so" ||
         name == "studiorender.so" ||
         name == "datacache.so" ||
         name == "filesystem_stdio.so" ||
         name == "filesystem_steam.so";
}

void sync_nographics_toggles()
{
}

void resolve_nographics_convar(convar_override& entry)
{
  if (entry.convar == nullptr && convar_system != nullptr)
  {
    entry.convar = convar_system->find_var(entry.name);
  }
}

void apply_nographics_convar_overrides()
{
  if (convar_system == nullptr)
  {
    return;
  }

  for (convar_override& entry : nographics_convar_overrides)
  {
    resolve_nographics_convar(entry);
    if (entry.convar == nullptr)
    {
      continue;
    }

    if (!entry.original_saved)
    {
      entry.original_value = entry.convar->get_int();
      entry.original_saved = true;
    }

    if (entry.convar->get_int() != entry.wanted_value)
    {
      entry.convar->set_int(entry.wanted_value);
    }
  }
}

void restore_nographics_convar_overrides()
{
  for (convar_override& entry : nographics_convar_overrides)
  {
    resolve_nographics_convar(entry);
    if (entry.convar == nullptr || !entry.original_saved)
    {
      continue;
    }

    if (entry.convar->get_int() != entry.original_value)
    {
      entry.convar->set_int(entry.original_value);
    }

    entry.original_saved = false;
  }
}

byte_patch particle_create_patch{};
byte_patch particle_precache_patch{};
byte_patch particle_effect_create_patch{};
byte_patch view_render_patch{};
byte_patch replay_screenshot_patch{};
byte_patch steam_rich_presence_patch{};
byte_patch cl_decay_lights_patch{};
byte_patch fps_max_min_patch{};
byte_patch engine_frame_busy_wait_patch{};
byte_patch engine_frame_usleep_patch{};
byte_patch mod_load_lighting_patch{};
byte_patch mod_load_worldlights_patch{};
byte_patch mod_load_texinfo_material_branch_patch{};
byte_patch sprite_load_model_patch{};
byte_patch overlay_mgr_load_overlays_patch{};
byte_patch shadow_render_patch{};
byte_patch static_prop_draw_patch{};
byte_patch engine_sound_emit_sound_internal_patch{};
byte_patch s_precache_sound_patch{};
byte_patch svc_bspdecal_process_patch{};
byte_patch r_draw_decals_all_0_patch{};
byte_patch r_draw_decals_all_1_patch{};
byte_patch v_render_view_patch{};
byte_patch material_system_begin_frame_patch{};
byte_patch material_system_swap_buffers_patch{};
byte_patch startup_video_patch{};
byte_patch video_mode_setup_startup_graphic_patch{};
constexpr std::size_t replay_ui_nullcheck_patch_count = 9;
constexpr std::size_t character_info_command_patch_count = 5;
std::array<byte_patch, replay_ui_nullcheck_patch_count> replay_ui_nullcheck_patches{};
std::array<byte_patch, character_info_command_patch_count> character_info_command_patches{};
byte_patch econ_item_definition_index_patch{};
byte_patch studio_render_draw_model_wrapper_patch{};
byte_patch econ_panel_flex_primary_patch{};
byte_patch econ_panel_flex_attachments_patch{};
byte_patch ragdoll_lru_update_patch{};
byte_patch particle_mgr_simulate_undrawn_patch{};
byte_patch temp_ents_update_patch{};
byte_patch rope_manager_draw_render_cache_patch{};
byte_patch init_caption_dictionary_patch{};
byte_patch parse_particle_effects_map_patch{};

char normalize_path_char(const char value)
{
  if (value == '\\')
  {
    return '/';
  }

  if (value >= 'A' && value <= 'Z')
  {
    return static_cast<char>(value - 'A' + 'a');
  }

  return value;
}

bool path_equals(const std::string_view path, const std::string_view expected)
{
  if (path.size() != expected.size())
  {
    return false;
  }

  for (std::size_t index = 0; index < expected.size(); ++index)
  {
    if (normalize_path_char(path[index]) != normalize_path_char(expected[index]))
    {
      return false;
    }
  }

  return true;
}

bool path_starts_with(const std::string_view path, const std::string_view prefix)
{
  if (path.size() < prefix.size())
  {
    return false;
  }

  return path_equals(path.substr(0, prefix.size()), prefix);
}

bool path_ends_with(const std::string_view path, const std::string_view suffix)
{
  if (path.size() < suffix.size())
  {
    return false;
  }

  return path_equals(path.substr(path.size() - suffix.size()), suffix);
}

bool path_contains(const std::string_view path, const std::string_view needle)
{
  if (needle.empty() || path.size() < needle.size())
  {
    return false;
  }

  for (std::size_t index = 0; index <= path.size() - needle.size(); ++index)
  {
    if (path_equals(path.substr(index, needle.size()), needle))
    {
      return true;
    }
  }

  return false;
}

bool path_contains_any(const std::string_view path, std::initializer_list<std::string_view> needles)
{
  for (const std::string_view needle : needles)
  {
    if (path_contains(path, needle))
    {
      return true;
    }
  }

  return false;
}

std::string_view file_extension(const std::string_view filename)
{
  const auto slash = filename.find_last_of("/\\");
  const auto dot = filename.find_last_of('.');
  if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
  {
    return {};
  }

  return filename.substr(dot);
}

bool is_soundscape_script(const std::string_view filename)
{
  return path_equals(filename, "scripts/soundscapes_manifest.txt") ||
         (path_starts_with(filename, "scripts/soundscapes_") && path_ends_with(filename, ".txt"));
}

bool is_required_model_asset(const std::string_view filename, const std::string_view extension)
{
  if (path_equals(extension, ".mdl") || path_equals(extension, ".phy"))
  {
    return true;
  }

  if (!path_equals(extension, ".ani") && !path_equals(extension, ".vvd") && !path_equals(extension, ".vtx"))
  {
    return false;
  }

  return path_contains_any(filename, {
    "models/player/",
    "models/bots/",
    "models/buildables/",
    "models/weapons/",
    "models/items/",
    "models/pickups/",
    "models/props_halloween/",
    "models/props_medieval/",
    "models/flag/",
    "models/custom/",
    "player/",
    "bots/",
    "buildables/",
    "weapons/",
    "empty.mdl",
    "error.mdl",
  });
}

bool should_block_file(const char* raw_filename)
{
  constexpr std::array<std::string_view, 18> blocked_extensions = {
    ".ani", ".wav", ".mp3", ".vvd", ".vtx", ".vfe", ".cache",
    ".jpg", ".png", ".tga", ".dds", ".bik", ".webm", ".vcd", ".pcf",
    ".cur", ".ico", ".dem",
  };

  if (raw_filename == nullptr)
  {
    return false;
  }

  const std::string_view filename{ raw_filename };
  if (filename.size() <= 3)
  {
    return false;
  }

  const std::string_view extension = file_extension(filename);
  if (path_equals(extension, ".cat") || path_equals(extension, ".cfg"))
  {
    return false;
  }

  if (path_equals(extension, ".bsp") || path_equals(extension, ".nav") || is_required_model_asset(filename, extension))
  {
    return false;
  }

  if (is_soundscape_script(filename) ||
      path_starts_with(filename, "materials/console/") ||
      path_starts_with(filename, "debug/"))
  {
    return false;
  }

  if (path_starts_with(filename, "replay/") ||
      path_starts_with(filename, "resource/replay/") ||
      path_starts_with(filename, "materials/vgui/replay/") ||
      path_starts_with(filename, "media/") ||
      path_starts_with(filename, "videos/") ||
      path_starts_with(filename, "cursors/") ||
      path_starts_with(filename, "resource/cursors/") ||
      path_starts_with(filename, "materials/cursors/"))
  {
    return true;
  }

  if (extension.empty())
  {
    return false;
  }

  if (path_equals(extension, ".vmt"))
  {
    return !path_contains(filename, "corner") &&
           !path_contains(filename, "hud") &&
           !path_contains(filename, "vgui") &&
           !path_contains(filename, "console");
  }

  if (path_contains(filename, "sound.cache") ||
      path_contains(filename, "tf2_sound") ||
      path_contains(filename, "game_sounds") ||
      path_starts_with(filename, "sound/player/footsteps"))
  {
    return false;
  }

  if (path_starts_with(filename, "/decal") ||
      path_starts_with(filename, "decal") ||
      path_starts_with(filename, "materials/decals/") ||
      path_starts_with(filename, "sprites/") ||
      path_contains(filename, "skybox") ||
      path_contains(filename, "detail") ||
      path_contains(filename, "ambient") ||
      (path_contains(filename, "soundscape") && !path_equals(extension, ".txt")))
  {
    return true;
  }

  for (const std::string_view blocked_extension : blocked_extensions)
  {
    if (path_equals(extension, blocked_extension))
    {
      return true;
    }
  }

  return false;
}

bool should_skip_texture_file(const char* raw_filename)
{
  if (raw_filename == nullptr)
  {
    return false;
  }

  const std::string_view filename{ raw_filename };
  return path_equals(file_extension(filename), ".vtf");
}

template <typename function_type>
bool hook_vtable(void** vtable, int index, void* hook, function_type* original)
{
  if (vtable == nullptr || hook == nullptr || original == nullptr)
  {
    return false;
  }

  *original = reinterpret_cast<function_type>(vtable[index]);
  return write_to_table(vtable, index, hook);
}

file_handle_t open_hook(void* this_ptr, const char* filename, const char* options, const char* path_id)
{
  if (should_block_file(filename))
  {
    return nullptr;
  }

  return open_original(this_ptr, filename, options, path_id);
}

bool precache_hook(void* this_ptr, const char* filename, const char* path_id)
{
  if (should_block_file(filename))
  {
    return false;
  }

  return precache_original(this_ptr, filename, path_id);
}

bool file_exists_hook(void* this_ptr, const char* filename, const char* path_id)
{
  if (should_block_file(filename))
  {
    return false;
  }

  return file_exists_original(this_ptr, filename, path_id);
}

bool read_file_hook(void* this_ptr, const char* filename, const char* path, void* buffer, int max_bytes, int starting_byte, void* alloc_fn)
{
  if (should_block_file(filename))
  {
    return false;
  }

  return read_file_original(this_ptr, filename, path, buffer, max_bytes, starting_byte, alloc_fn);
}

const char* find_next_hook(void* this_ptr, file_find_handle_t handle)
{
  const char* filename = nullptr;
  do
  {
    filename = find_next_original(this_ptr, handle);
  }
  while (filename != nullptr && should_block_file(filename));

  return filename;
}

const char* find_first_hook(void* this_ptr, const char* wildcard, file_find_handle_t* handle)
{
  const char* filename = find_first_original(this_ptr, wildcard, handle);
  while (filename != nullptr && handle != nullptr && should_block_file(filename))
  {
    filename = find_next_original(this_ptr, *handle);
  }

  return filename;
}

int async_read_multiple_hook(void* this_ptr, const file_async_request* requests, int request_count, void* controls)
{
  if (requests == nullptr || request_count <= 0)
  {
    return async_read_multiple_original(this_ptr, requests, request_count, controls);
  }

  bool has_blocked_request = false;
  bool has_allowed_request = false;
  for (int index = 0; index < request_count; ++index)
  {
    if (should_block_file(requests[index].filename))
    {
      has_blocked_request = true;
    }
    else
    {
      has_allowed_request = true;
    }
  }

  if (!has_blocked_request)
  {
    return async_read_multiple_original(this_ptr, requests, request_count, controls);
  }

  if (has_allowed_request && controls != nullptr)
  {
    return async_read_multiple_original(this_ptr, requests, request_count, controls);
  }

  for (int index = 0; index < request_count; ++index)
  {
    if (should_block_file(requests[index].filename) && requests[index].callback != nullptr)
    {
      requests[index].callback(requests[index], 0, fs_async_err_fileopen);
    }
  }

  if (!has_allowed_request)
  {
    return fs_async_err_fileopen;
  }

  std::vector<file_async_request> allowed_requests{};
  allowed_requests.reserve(static_cast<std::size_t>(request_count));
  for (int index = 0; index < request_count; ++index)
  {
    if (!should_block_file(requests[index].filename))
    {
      allowed_requests.emplace_back(requests[index]);
    }
  }

  return async_read_multiple_original(
    this_ptr,
    allowed_requests.data(),
    static_cast<int>(allowed_requests.size()),
    controls);
}

file_handle_t open_ex_hook(void* this_ptr, const char* filename, const char* options, unsigned int flags, const char* path_id, char** resolved_filename)
{
  if (should_block_file(filename))
  {
    return nullptr;
  }

  return open_ex_original(this_ptr, filename, options, flags, path_id, resolved_filename);
}

int read_file_ex_hook(void* this_ptr, const char* filename, const char* path, void** buffer, bool null_terminate, bool optimal_alloc, int max_bytes, int starting_byte, void* alloc_fn)
{
  if (should_block_file(filename))
  {
    return 0;
  }

  return read_file_ex_original(this_ptr, filename, path, buffer, null_terminate, optimal_alloc, max_bytes, starting_byte, alloc_fn);
}

void add_files_to_cache_hook(void* this_ptr, file_cache_handle_t cache_id, const char** filenames, int filename_count, const char* path_id)
{
  if (filenames == nullptr || filename_count <= 0)
  {
    return;
  }

  std::vector<const char*> allowed_filenames{};
  allowed_filenames.reserve(static_cast<std::size_t>(filename_count));

  for (int index = 0; index < filename_count; ++index)
  {
    const char* filename = filenames[index];
    if (filename != nullptr && !should_block_file(filename))
    {
      allowed_filenames.emplace_back(filename);
    }
  }

  if (allowed_filenames.empty())
  {
    return;
  }

  add_files_to_cache_original(
    this_ptr,
    cache_id,
    allowed_filenames.data(),
    static_cast<int>(allowed_filenames.size()),
    path_id);
}

std::int64_t bone_setup_attachment_matrices_hook(
  void* studio_hdr,
  const void* parent_transform,
  int bone_index,
  void* bone_matrix_buffer,
  void* attachment_data,
  std::int64_t origin_buffer,
  int origin_count)
{
  if (studio_hdr == nullptr || bone_matrix_buffer == nullptr || attachment_data == nullptr)
  {
    return 0;
  }

  return bone_setup_attachment_matrices_original(
    studio_hdr,
    parent_transform,
    bone_index,
    bone_matrix_buffer,
    attachment_data,
    origin_buffer,
    origin_count);
}

void* texture_load_bits_hook(void* this_ptr, const char* cache_file_name, char** resolved_filename)
{
  if (should_skip_texture_file(cache_file_name) &&
      get_scratch_vtf_texture != nullptr &&
      handle_file_load_failed_texture != nullptr)
  {
    (void)resolved_filename;
    void* scratch_texture = get_scratch_vtf_texture(this_ptr);
    return handle_file_load_failed_texture(this_ptr, scratch_texture);
  }

  return texture_load_bits_original(this_ptr, cache_file_name, resolved_filename);
}

Texture* create_render_target_texture_hook(
  void* this_ptr,
  int width,
  int height,
  render_target_size_mode size_mode,
  image_format format,
  material_render_target_depth depth)
{
  (void)width;
  (void)height;
  return create_render_target_texture_original(this_ptr, 1, 1, size_mode, format, depth);
}

Texture* create_named_render_target_texture_ex_hook(
  void* this_ptr,
  const char* render_target_name,
  int width,
  int height,
  render_target_size_mode size_mode,
  image_format format,
  material_render_target_depth depth,
  unsigned int texture_flags,
  unsigned int render_target_flags)
{
  (void)width;
  (void)height;
  return create_named_render_target_texture_ex_original(this_ptr, render_target_name, 1, 1, size_mode, format, depth, texture_flags, render_target_flags);
}

Texture* create_named_render_target_texture_hook(
  void* this_ptr,
  const char* render_target_name,
  int width,
  int height,
  render_target_size_mode size_mode,
  image_format format,
  material_render_target_depth depth,
  bool clamp_tex_coords,
  bool auto_mip_map)
{
  (void)width;
  (void)height;
  return create_named_render_target_texture_original(this_ptr, render_target_name, 1, 1, size_mode, format, depth, clamp_tex_coords, auto_mip_map);
}

Texture* create_named_render_target_texture_ex2_hook(
  void* this_ptr,
  const char* render_target_name,
  int width,
  int height,
  render_target_size_mode size_mode,
  image_format format,
  material_render_target_depth depth,
  unsigned int texture_flags,
  unsigned int render_target_flags)
{
  (void)width;
  (void)height;
  return create_named_render_target_texture_ex2_original(this_ptr, render_target_name, 1, 1, size_mode, format, depth, texture_flags, render_target_flags);
}

void studio_render_draw_model_hook(void* this_ptr, void* results, const void* info, void* bone_to_world, void* flex_weights, void* flex_delayed_weights, const void* model_origin, int flags)
{
  (void)this_ptr;
  (void)results;
  (void)info;
  (void)bone_to_world;
  (void)flex_weights;
  (void)flex_delayed_weights;
  (void)model_origin;
  (void)flags;
}

void studio_render_draw_model_static_prop_hook(void* this_ptr, const void* draw_info, const void* model_to_world, int flags)
{
  (void)this_ptr;
  (void)draw_info;
  (void)model_to_world;
  (void)flags;
}

void studio_render_draw_static_prop_decals_hook(void* this_ptr, const void* draw_info, const void* model_to_world)
{
  (void)this_ptr;
  (void)draw_info;
  (void)model_to_world;
}

void studio_render_draw_static_prop_shadows_hook(void* this_ptr, const void* draw_info, const void* model_to_world, int flags)
{
  (void)this_ptr;
  (void)draw_info;
  (void)model_to_world;
  (void)flags;
}

void studio_render_add_decal_hook(void* this_ptr, void* handle, void* studio_hdr, void* bone_to_world, const void* ray, const void* decal_up, void* decal_material, float radius, int body, bool no_pokethru, int max_lod_to_decal)
{
  (void)this_ptr;
  (void)handle;
  (void)studio_hdr;
  (void)bone_to_world;
  (void)ray;
  (void)decal_up;
  (void)decal_material;
  (void)radius;
  (void)body;
  (void)no_pokethru;
  (void)max_lod_to_decal;
}

void studio_render_add_shadow_hook(void* this_ptr, void* material, void* proxy_data, void* flashlight_state, void* world_to_texture, void* flashlight_depth_texture)
{
  (void)this_ptr;
  (void)material;
  (void)proxy_data;
  (void)flashlight_state;
  (void)world_to_texture;
  (void)flashlight_depth_texture;
}

void studio_render_draw_model_array_hook(void* this_ptr, const void* draw_info, int array_count, void* instance_data, int instance_stride, int flags)
{
  (void)this_ptr;
  (void)draw_info;
  (void)array_count;
  (void)instance_data;
  (void)instance_stride;
  (void)flags;
}

void mdl_cache_touch_all_data_hook(void* this_ptr, unsigned short handle)
{
  (void)this_ptr;
  (void)handle;
}

void* resolve_rip_target(std::uint8_t* instruction, int displacement_offset, int instruction_size)
{
  const auto displacement = *reinterpret_cast<std::int32_t*>(instruction + displacement_offset);
  return instruction + instruction_size + displacement;
}

std::uint8_t* scan_module_patch(const char* module_name, const char* signature, int offset)
{
  auto* match = reinterpret_cast<std::uint8_t*>(sigscan_module(module_name, signature));
  if (match == nullptr)
  {
    return nullptr;
  }

  return match + offset;
}

std::uint8_t* scan_client_patch(const char* signature, int offset)
{
  return scan_module_patch(client_module_name, signature, offset);
}

bool initialize_optional_patch(byte_patch& patch,
                               const char* module_name,
                               const char* signature,
                               int offset,
                               std::initializer_list<std::uint8_t> patch_bytes,
                               const char* patch_name)
{
  auto* patch_site = scan_module_patch(module_name, signature, offset);
  if (patch_site == nullptr)
  {
    print("[nographics] optional patch scan failed name=%s module=%s\n", patch_name, module_name);
    return false;
  }

  patch = byte_patch(patch_site, patch_bytes);
  return true;
}

bool initialize_core_render_patch(byte_patch& patch,
                                  const char* module_name,
                                  const char* signature,
                                  int offset,
                                  std::initializer_list<std::uint8_t> patch_bytes,
                                  const char* patch_name)
{
  auto* patch_site = scan_module_patch(module_name, signature, offset);
  if (patch_site == nullptr)
  {
    print("[nographics] core patch scan failed name=%s module=%s\n", patch_name, module_name);
    return false;
  }

  patch = byte_patch(patch_site, patch_bytes);
  return true;
}

bool initialize_relative_jump_patch(byte_patch& patch,
                                    const char* module_name,
                                    const char* signature,
                                    int target_delta,
                                    int patch_size,
                                    const char* patch_name)
{
  std::uint8_t* patch_site = scan_module_patch(module_name, signature, 0);
  if (patch_site == nullptr)
  {
    print("[nographics] optional patch scan failed name=%s module=%s\n", patch_name, module_name);
    return false;
  }

  const std::int32_t relative = target_delta - relative_jump_size;
  std::vector<std::uint8_t> patch_bytes(static_cast<std::size_t>(patch_size), 0x90);
  patch_bytes[0] = 0xE9;
  std::memcpy(patch_bytes.data() + 1, &relative, sizeof(relative));
  patch = byte_patch(patch_site, std::move(patch_bytes));
  return true;
}

void initialize_engine_render_patches()
{
  if (engine_render_patches_initialized || !module_is_loaded("engine.so"))
  {
    return;
  }

  engine_render_patches_initialized = true;
  initialize_optional_patch(startup_video_patch, "engine.so", sigs::startup_video, 0, { 0x31, 0xC0, 0xC3 }, "startup_video");
  initialize_optional_patch(video_mode_setup_startup_graphic_patch, "engine.so", sigs::video_mode_setup_startup_graphic, 0, { 0xC3 }, "video_mode_setup_startup_graphic");
  initialize_optional_patch(cl_decay_lights_patch, "engine.so", sigs::cl_decay_lights, 0, { 0xC3 }, "cl_decay_lights");
  initialize_optional_patch(fps_max_min_patch, "engine.so", sigs::engine_fps_max_min_clamp, 7, { 0x90, 0xE9 }, "engine_fps_max_min_clamp");
  if (sleep_pacing_patch_enabled())
  {
    initialize_relative_jump_patch(engine_frame_busy_wait_patch, "engine.so", sigs::engine_frame_busy_wait, engine_frame_busy_wait_usleep_delta, 5, "engine_frame_busy_wait");
  }
  initialize_optional_patch(engine_frame_usleep_patch, "engine.so", sigs::engine_frame_busy_wait, engine_frame_usleep_call_offset, { 0x90, 0x90, 0x90, 0x90, 0x90 }, "engine_frame_usleep");
  initialize_optional_patch(mod_load_lighting_patch, "engine.so", sigs::mod_load_lighting, 0, { 0x31, 0xC0, 0xC3 }, "mod_load_lighting");
  initialize_optional_patch(mod_load_worldlights_patch, "engine.so", sigs::mod_load_worldlights, 0, { 0x31, 0xC0, 0xC3 }, "mod_load_worldlights");
  initialize_optional_patch(
    mod_load_texinfo_material_branch_patch,
    "engine.so",
    sigs::mod_load_texinfo_material_branch,
    17,
    { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 },
    "mod_load_texinfo_material_branch");
  initialize_optional_patch(sprite_load_model_patch, "engine.so", sigs::sprite_load_model, 0, { 0xC3 }, "sprite_load_model");
  initialize_optional_patch(overlay_mgr_load_overlays_patch, "engine.so", sigs::overlay_mgr_load_overlays, 0, { 0xB0, 0x01, 0xC3 }, "overlay_mgr_load_overlays");
  initialize_optional_patch(shadow_render_patch, "engine.so", sigs::shadow_mgr_render_shadows, 0, { 0xC3 }, "shadow_mgr_render_shadows");
  initialize_optional_patch(static_prop_draw_patch, "engine.so", sigs::static_prop_mgr_draw_static_props, 0, { 0xC3 }, "static_prop_mgr_draw_static_props");
  initialize_optional_patch(engine_sound_emit_sound_internal_patch, "engine.so", sigs::engine_sound_emit_sound_internal, 0, { 0xC3 }, "engine_sound_emit_sound_internal");
  initialize_optional_patch(s_precache_sound_patch, "engine.so", sigs::s_precache_sound, 0, { 0x31, 0xC0, 0xC3 }, "s_precache_sound");
  initialize_optional_patch(svc_bspdecal_process_patch, "engine.so", sigs::svc_bspdecal_process, 0, { 0xB0, 0x01, 0xC3 }, "svc_bspdecal_process");
  initialize_optional_patch(r_draw_decals_all_0_patch, "engine.so", sigs::r_draw_decals_all_0, 0, { 0xC3 }, "r_draw_decals_all_0");
  initialize_optional_patch(r_draw_decals_all_1_patch, "engine.so", sigs::r_draw_decals_all_1, 0, { 0xC3 }, "r_draw_decals_all_1");
  initialize_optional_patch(v_render_view_patch, "engine.so", sigs::v_render_view, 0, { 0xC3 }, "v_render_view");
}

void initialize_materialsystem_render_patches()
{
  if (materialsystem_render_patches_initialized || !module_is_loaded("materialsystem.so"))
  {
    return;
  }

  materialsystem_render_patches_initialized = true;
  initialize_optional_patch(material_system_begin_frame_patch, "materialsystem.so", sigs::material_system_begin_frame, 0, { 0xC3 }, "material_system_begin_frame");
  initialize_optional_patch(material_system_swap_buffers_patch, "materialsystem.so", sigs::material_system_swap_buffers, 0, { 0xC3 }, "material_system_swap_buffers");
}

void initialize_client_textmode_cpu_patches()
{
  if (client_textmode_cpu_patches_initialized || !module_is_loaded(client_module_name))
  {
    return;
  }

  bool initialized_any_patch = false;
  initialized_any_patch = initialize_optional_patch(ragdoll_lru_update_patch, client_module_name, sigs::client_ragdoll_lru_update, 0, { 0xC3 }, "client_ragdoll_lru_update") || initialized_any_patch;
  initialized_any_patch = initialize_optional_patch(particle_mgr_simulate_undrawn_patch, client_module_name, sigs::client_particle_mgr_simulate_undrawn, 0, { 0xC3 }, "client_particle_mgr_simulate_undrawn") || initialized_any_patch;
  initialized_any_patch = initialize_optional_patch(temp_ents_update_patch, client_module_name, sigs::client_temp_ents_update, 0, { 0xC3 }, "client_temp_ents_update") || initialized_any_patch;
  initialized_any_patch = initialize_optional_patch(rope_manager_draw_render_cache_patch, client_module_name, sigs::client_rope_manager_draw_render_cache, 0, { 0xC3 }, "client_rope_manager_draw_render_cache") || initialized_any_patch;
  initialized_any_patch = initialize_optional_patch(init_caption_dictionary_patch, client_module_name, sigs::client_init_caption_dictionary, 0, { 0x31, 0xC0, 0xC3 }, "client_init_caption_dictionary") || initialized_any_patch;
  initialized_any_patch = initialize_optional_patch(parse_particle_effects_map_patch, client_module_name, sigs::client_parse_particle_effects_map, 0, { 0xC3 }, "client_parse_particle_effects_map") || initialized_any_patch;
  client_textmode_cpu_patches_initialized = initialized_any_patch;
}

void initialize_optional_render_patches()
{
  initialize_engine_render_patches();
  initialize_materialsystem_render_patches();
  initialize_client_textmode_cpu_patches();
}

void restore_optional_render_patches()
{
  startup_video_patch.restore();
  video_mode_setup_startup_graphic_patch.restore();
  cl_decay_lights_patch.restore();
  fps_max_min_patch.restore();
  engine_frame_busy_wait_patch.restore();
  engine_frame_usleep_patch.restore();
  mod_load_lighting_patch.restore();
  mod_load_worldlights_patch.restore();
  mod_load_texinfo_material_branch_patch.restore();
  sprite_load_model_patch.restore();
  overlay_mgr_load_overlays_patch.restore();
  shadow_render_patch.restore();
  static_prop_draw_patch.restore();
  engine_sound_emit_sound_internal_patch.restore();
  s_precache_sound_patch.restore();
  svc_bspdecal_process_patch.restore();
  r_draw_decals_all_0_patch.restore();
  r_draw_decals_all_1_patch.restore();
  v_render_view_patch.restore();
  material_system_begin_frame_patch.restore();
  material_system_swap_buffers_patch.restore();
  ragdoll_lru_update_patch.restore();
  particle_mgr_simulate_undrawn_patch.restore();
  temp_ents_update_patch.restore();
  rope_manager_draw_render_cache_patch.restore();
  init_caption_dictionary_patch.restore();
  parse_particle_effects_map_patch.restore();
  optional_render_patches_applied = false;
}

bool apply_optional_patch(byte_patch& patch, const char* patch_name)
{
  if (!patch.valid())
  {
    return false;
  }

  if (!patch.apply())
  {
    print("[nographics] optional patch apply failed name=%s\n", patch_name);
    return false;
  }

  return true;
}

bool apply_optional_render_patches()
{
  initialize_optional_render_patches();

  bool applied_any_patch = false;
  applied_any_patch = apply_optional_patch(startup_video_patch, "startup_video") || applied_any_patch;
  applied_any_patch = apply_optional_patch(video_mode_setup_startup_graphic_patch, "video_mode_setup_startup_graphic") || applied_any_patch;
  applied_any_patch = apply_optional_patch(cl_decay_lights_patch, "cl_decay_lights") || applied_any_patch;
  applied_any_patch = apply_optional_patch(fps_max_min_patch, "engine_fps_max_min_clamp") || applied_any_patch;
  applied_any_patch = apply_optional_patch(engine_frame_busy_wait_patch, "engine_frame_busy_wait") || applied_any_patch;
  if (config.misc.exploits.no_engine_sleep)
  {
    applied_any_patch = apply_optional_patch(engine_frame_usleep_patch, "engine_frame_usleep") || applied_any_patch;
  }
  else
  {
    engine_frame_usleep_patch.restore();
  }
  applied_any_patch = apply_optional_patch(mod_load_lighting_patch, "mod_load_lighting") || applied_any_patch;
  applied_any_patch = apply_optional_patch(mod_load_worldlights_patch, "mod_load_worldlights") || applied_any_patch;
  applied_any_patch = apply_optional_patch(mod_load_texinfo_material_branch_patch, "mod_load_texinfo_material_branch") || applied_any_patch;
  applied_any_patch = apply_optional_patch(sprite_load_model_patch, "sprite_load_model") || applied_any_patch;
  applied_any_patch = apply_optional_patch(overlay_mgr_load_overlays_patch, "overlay_mgr_load_overlays") || applied_any_patch;
  applied_any_patch = apply_optional_patch(shadow_render_patch, "shadow_mgr_render_shadows") || applied_any_patch;
  applied_any_patch = apply_optional_patch(static_prop_draw_patch, "static_prop_mgr_draw_static_props") || applied_any_patch;
  applied_any_patch = apply_optional_patch(engine_sound_emit_sound_internal_patch, "engine_sound_emit_sound_internal") || applied_any_patch;
  applied_any_patch = apply_optional_patch(s_precache_sound_patch, "s_precache_sound") || applied_any_patch;
  applied_any_patch = apply_optional_patch(svc_bspdecal_process_patch, "svc_bspdecal_process") || applied_any_patch;
  applied_any_patch = apply_optional_patch(r_draw_decals_all_0_patch, "r_draw_decals_all_0") || applied_any_patch;
  applied_any_patch = apply_optional_patch(r_draw_decals_all_1_patch, "r_draw_decals_all_1") || applied_any_patch;
  applied_any_patch = apply_optional_patch(v_render_view_patch, "v_render_view") || applied_any_patch;
  applied_any_patch = apply_optional_patch(material_system_begin_frame_patch, "material_system_begin_frame") || applied_any_patch;
  applied_any_patch = apply_optional_patch(material_system_swap_buffers_patch, "material_system_swap_buffers") || applied_any_patch;
  applied_any_patch = apply_optional_patch(ragdoll_lru_update_patch, "client_ragdoll_lru_update") || applied_any_patch;
  applied_any_patch = apply_optional_patch(particle_mgr_simulate_undrawn_patch, "client_particle_mgr_simulate_undrawn") || applied_any_patch;
  applied_any_patch = apply_optional_patch(temp_ents_update_patch, "client_temp_ents_update") || applied_any_patch;
  applied_any_patch = apply_optional_patch(rope_manager_draw_render_cache_patch, "client_rope_manager_draw_render_cache") || applied_any_patch;
  applied_any_patch = apply_optional_patch(init_caption_dictionary_patch, "client_init_caption_dictionary") || applied_any_patch;
  applied_any_patch = apply_optional_patch(parse_particle_effects_map_patch, "client_parse_particle_effects_map") || applied_any_patch;
  optional_render_patches_applied = optional_render_patches_applied || applied_any_patch;
  return applied_any_patch;
}

bool apply_render_patch_if_valid(byte_patch& patch, const char* patch_name)
{
  if (!patch.valid())
  {
    return true;
  }

  if (!patch.apply())
  {
    print("[nographics] render patch apply failed name=%s\n", patch_name);
    return false;
  }

  return true;
}

bool initialize_replay_ui_nullcheck_patches()
{
  constexpr int call_offset_after_global_load = 13;
  constexpr int call_offset_after_saved_return = 16;
  constexpr int call_offset_at_start = 6;

  std::array<std::uint8_t*, replay_ui_nullcheck_patch_count> patch_sites = {
    scan_client_patch(sigs::replay_ui_nullcheck_0, call_offset_after_global_load),
    scan_client_patch(sigs::replay_ui_nullcheck_1, call_offset_after_global_load),
    scan_client_patch(sigs::replay_ui_nullcheck_2, call_offset_after_saved_return),
    scan_client_patch(sigs::replay_ui_nullcheck_3, call_offset_at_start),
    scan_client_patch(sigs::replay_ui_nullcheck_4, call_offset_after_global_load),
    scan_client_patch(sigs::replay_ui_nullcheck_5, call_offset_after_global_load),
    scan_client_patch(sigs::replay_ui_nullcheck_6, call_offset_after_global_load),
    scan_client_patch(sigs::replay_ui_nullcheck_7, call_offset_after_global_load),
    scan_client_patch(sigs::replay_ui_nullcheck_8, call_offset_after_global_load),
  };

  bool initialized_any_patch = false;
  for (std::size_t index = 0; index < patch_sites.size(); ++index)
  {
    if (patch_sites[index] == nullptr)
    {
      print("[nographics] optional replay ui nullcheck patch scan failed index=%zu\n", index);
      continue;
    }

    if (index == 6 || index == 8)
    {
      replay_ui_nullcheck_patches[index] = byte_patch(patch_sites[index], { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 });
    }
    else
    {
      replay_ui_nullcheck_patches[index] = byte_patch(patch_sites[index], { 0x30, 0xC0, 0x90 });
    }

    initialized_any_patch = true;
  }

  return initialized_any_patch;
}

bool initialize_extra_crashfix_patches()
{
  constexpr std::array<const char*, character_info_command_patch_count> character_info_command_sigs = {
    sigs::character_info_open,
    sigs::character_info_open_direct,
    sigs::character_info_open_backpack,
    sigs::character_info_open_crafting,
    sigs::character_info_open_armory,
  };

  auto* item_definition_index = scan_client_patch(sigs::econ_item_view_get_item_definition_index, 0);
  if (item_definition_index == nullptr)
  {
    print("[nographics] optional econ item definition index patch scan failed\n");
  }

  bool initialized_any_patch = false;
  for (std::size_t index = 0; index < character_info_command_sigs.size(); ++index)
  {
    auto* patch_site = scan_client_patch(character_info_command_sigs[index], 0);
    if (patch_site == nullptr)
    {
      print("[nographics] optional character info command patch scan failed index=%zu\n", index);
      continue;
    }

    character_info_command_patches[index] = byte_patch(patch_site, { 0x31, 0xC0, 0xC3 });
    initialized_any_patch = true;
  }

  if (item_definition_index != nullptr)
  {
    econ_item_definition_index_patch = byte_patch(item_definition_index, {
      0x48, 0x8B, 0x47, 0x08,
      0x48, 0x85, 0xC0,
      0x75, 0x0F,
      0x48, 0x8B, 0x07,
      0x48, 0x85, 0xC0,
      0x74, 0x0B,
      0x8B, 0x80, 0xBC, 0x00, 0x00, 0x00,
      0xC3,
      0x8B, 0x40, 0x20,
      0xC3,
      0x31, 0xC0,
      0xC3,
      0x90,
    });
    initialized_any_patch = true;
  }

  if (initialize_optional_patch(studio_render_draw_model_wrapper_patch,
                                client_module_name,
                                sigs::studio_render_draw_model_wrapper,
                                0,
                                { 0xC3 },
                                "studio_render_draw_model_wrapper"))
  {
    initialized_any_patch = true;
  }

  if (initialize_optional_patch(econ_panel_flex_primary_patch,
                                client_module_name,
                                sigs::client_econ_panel_flex_primary,
                                0,
                                { 0x31, 0xC0, 0xC3 },
                                "client_econ_panel_flex_primary"))
  {
    initialized_any_patch = true;
  }

  if (initialize_optional_patch(econ_panel_flex_attachments_patch,
                                client_module_name,
                                sigs::client_econ_panel_flex_attachments,
                                0,
                                { 0x31, 0xC0, 0xC3 },
                                "client_econ_panel_flex_attachments"))
  {
    initialized_any_patch = true;
  }

  return initialized_any_patch;
}

void restore_extra_crashfix_patches()
{
  for (byte_patch& patch : character_info_command_patches)
  {
    patch.restore();
  }

  econ_item_definition_index_patch.restore();
  studio_render_draw_model_wrapper_patch.restore();
  econ_panel_flex_primary_patch.restore();
  econ_panel_flex_attachments_patch.restore();
}

void restore_render_patch_objects()
{
  particle_create_patch.restore();
  particle_precache_patch.restore();
  particle_effect_create_patch.restore();
  view_render_patch.restore();
  replay_screenshot_patch.restore();
  steam_rich_presence_patch.restore();
  restore_optional_render_patches();

  for (auto& patch : replay_ui_nullcheck_patches)
  {
    patch.restore();
  }

  restore_extra_crashfix_patches();
}

bool initialize_render_patches()
{
  if (render_patches_initialized)
  {
    return render_patches_ready;
  }

  if (!module_is_loaded(client_module_name))
  {
    return false;
  }

  render_patches_initialized = true;

  std::size_t core_patch_count = 0;
  // Keep animation events alive in textmode. Stubbing this can leave client animation
  // state stale enough to break hitscan hitbox alignment.
  if (initialize_core_render_patch(particle_create_patch, client_module_name, sigs::particle_property_create, 0, { 0x31, 0xC0, 0xC3 }, "particle_property_create"))
  {
    ++core_patch_count;
  }
  if (initialize_core_render_patch(particle_precache_patch, client_module_name, sigs::particle_system_precache, 0, { 0x31, 0xC0, 0xC3 }, "particle_system_precache"))
  {
    ++core_patch_count;
  }
  if (initialize_core_render_patch(particle_effect_create_patch, client_module_name, sigs::particle_effect_create_event, 0, { 0x31, 0xC0, 0xC3 }, "particle_effect_create_event"))
  {
    ++core_patch_count;
  }
  if (initialize_core_render_patch(view_render_patch, client_module_name, sigs::view_render_render, 0, { 0xC3 }, "view_render_render"))
  {
    ++core_patch_count;
  }

  initialize_optional_patch(replay_screenshot_patch, client_module_name, sigs::replay_screenshot_render, 0, { 0xB0, 0x01, 0xC3 }, "replay_screenshot_render");
  initialize_optional_patch(steam_rich_presence_patch, client_module_name, sigs::client_update_steam_rich_presence, 0, { 0xC3 }, "client_update_steam_rich_presence");
  initialize_replay_ui_nullcheck_patches();
  initialize_extra_crashfix_patches();

  render_patches_ready = core_patch_count != 0;
  if (!render_patches_ready)
  {
    print("[nographics] no core render patches initialized\n");
    render_patches_initialized = false;
  }

  return render_patches_ready;
}

void apply_render_patches()
{
  const bool core_patches_ready = initialize_render_patches();
  bool ok = true;

  if (core_patches_ready)
  {
    ok = apply_render_patch_if_valid(particle_create_patch, "particle_property_create") && ok;
    ok = apply_render_patch_if_valid(particle_precache_patch, "particle_system_precache") && ok;
    ok = apply_render_patch_if_valid(particle_effect_create_patch, "particle_effect_create_event") && ok;
    ok = apply_render_patch_if_valid(view_render_patch, "view_render_render") && ok;
    ok = apply_render_patch_if_valid(replay_screenshot_patch, "replay_screenshot_render") && ok;
    ok = apply_render_patch_if_valid(steam_rich_presence_patch, "client_update_steam_rich_presence") && ok;
    for (auto& patch : replay_ui_nullcheck_patches)
    {
      ok = apply_render_patch_if_valid(patch, "replay_ui_nullcheck") && ok;
    }

    for (byte_patch& patch : character_info_command_patches)
    {
      ok = apply_render_patch_if_valid(patch, "character_info_command") && ok;
    }
    ok = apply_render_patch_if_valid(econ_item_definition_index_patch, "econ_item_definition_index") && ok;
    ok = apply_render_patch_if_valid(studio_render_draw_model_wrapper_patch, "studio_render_draw_model_wrapper") && ok;
    ok = apply_render_patch_if_valid(econ_panel_flex_primary_patch, "client_econ_panel_flex_primary") && ok;
    ok = apply_render_patch_if_valid(econ_panel_flex_attachments_patch, "client_econ_panel_flex_attachments") && ok;
  }

  if (!ok)
  {
    restore_render_patch_objects();
    render_patches_applied = false;
    optional_render_patches_applied = false;
    print("[nographics] render patch apply failed\n");
    return;
  }

  apply_optional_render_patches();

  render_patches_applied = render_patches_applied || core_patches_ready;
}

void restore_render_patches()
{
  if (!render_patches_applied && !optional_render_patches_applied)
  {
    return;
  }

  restore_render_patch_objects();
  render_patches_applied = false;
  optional_render_patches_applied = false;
}

void disable_file_system_hooks();
void disable_texture_load_hook();
void disable_bone_setup_attachment_matrices_guard();
void disable_material_system_render_target_hooks();
void disable_studio_render_hooks();
void disable_mdl_cache_touch_all_data_hook();

void enable_bone_setup_attachment_matrices_guard()
{
  if (bone_setup_attachment_matrices_hooked ||
      bone_setup_attachment_matrices_hook_failed ||
      !module_is_loaded(client_module_name))
  {
    return;
  }

  bone_setup_attachment_matrices_original =
    reinterpret_cast<bone_setup_attachment_matrices_fn>(
      sigscan_module(client_module_name, sigs::bone_setup_attachment_matrices));
  if (bone_setup_attachment_matrices_original == nullptr)
  {
    bone_setup_attachment_matrices_hook_failed = true;
    print("[nographics] bone-setup attachment matrices scan failed\n");
    return;
  }

  bone_setup_attachment_matrices_funchook = funchook_create();
  if (bone_setup_attachment_matrices_funchook == nullptr)
  {
    bone_setup_attachment_matrices_hook_failed = true;
    bone_setup_attachment_matrices_original = nullptr;
    print("[nographics] bone-setup attachment matrices hook create failed\n");
    return;
  }

  int result = funchook_prepare(
    bone_setup_attachment_matrices_funchook,
    reinterpret_cast<void**>(&bone_setup_attachment_matrices_original),
    reinterpret_cast<void*>(bone_setup_attachment_matrices_hook));
  if (result != 0)
  {
    bone_setup_attachment_matrices_hook_failed = true;
    funchook_destroy(bone_setup_attachment_matrices_funchook);
    bone_setup_attachment_matrices_funchook = nullptr;
    bone_setup_attachment_matrices_original = nullptr;
    print("[nographics] bone-setup attachment matrices hook prepare failed result=%d\n", result);
    return;
  }

  result = funchook_install(bone_setup_attachment_matrices_funchook, 0);
  if (result != 0)
  {
    bone_setup_attachment_matrices_hook_failed = true;
    funchook_destroy(bone_setup_attachment_matrices_funchook);
    bone_setup_attachment_matrices_funchook = nullptr;
    bone_setup_attachment_matrices_original = nullptr;
    print("[nographics] bone-setup attachment matrices hook install failed result=%d\n", result);
    return;
  }

  bone_setup_attachment_matrices_hooked = true;
  print("[nographics] bone-setup attachment matrices guard enabled\n");
}

void disable_bone_setup_attachment_matrices_guard()
{
  if (bone_setup_attachment_matrices_funchook == nullptr)
  {
    bone_setup_attachment_matrices_hooked = false;
    return;
  }

  if (bone_setup_attachment_matrices_hooked)
  {
    const int result = funchook_uninstall(bone_setup_attachment_matrices_funchook, 0);
    if (result != 0)
    {
      print("[nographics] bone-setup attachment matrices uninstall failed result=%d\n", result);
    }
  }

  funchook_destroy(bone_setup_attachment_matrices_funchook);
  bone_setup_attachment_matrices_funchook = nullptr;
  bone_setup_attachment_matrices_original = nullptr;
  bone_setup_attachment_matrices_hooked = false;
}

void enable_texture_load_hook()
{
  if (texture_load_bits_hooked || texture_load_bits_hook_failed || !module_is_loaded("materialsystem.so"))
  {
    return;
  }

  texture_load_bits_original =
    reinterpret_cast<texture_load_bits_fn>(sigscan_module("materialsystem.so", sigs::material_system_texture_load_bits));
  get_scratch_vtf_texture =
    reinterpret_cast<get_scratch_vtf_texture_fn>(sigscan_module("materialsystem.so", sigs::material_system_get_scratch_vtf_texture));
  handle_file_load_failed_texture =
    reinterpret_cast<handle_file_load_failed_texture_fn>(sigscan_module("materialsystem.so", sigs::material_system_handle_file_load_failed_texture));

  if (texture_load_bits_original == nullptr ||
      get_scratch_vtf_texture == nullptr ||
      handle_file_load_failed_texture == nullptr)
  {
    texture_load_bits_hook_failed = true;
    print("[nographics] texture load hook scan failed load=%p scratch=%p failed=%p\n",
          reinterpret_cast<void*>(texture_load_bits_original),
          reinterpret_cast<void*>(get_scratch_vtf_texture),
          reinterpret_cast<void*>(handle_file_load_failed_texture));
    return;
  }

  texture_load_bits_funchook = funchook_create();
  if (texture_load_bits_funchook == nullptr)
  {
    texture_load_bits_hook_failed = true;
    print("[nographics] texture load hook create failed\n");
    return;
  }

  int result = funchook_prepare(
    texture_load_bits_funchook,
    reinterpret_cast<void**>(&texture_load_bits_original),
    reinterpret_cast<void*>(texture_load_bits_hook));
  if (result != 0)
  {
    texture_load_bits_hook_failed = true;
    funchook_destroy(texture_load_bits_funchook);
    texture_load_bits_funchook = nullptr;
    texture_load_bits_original = nullptr;
    print("[nographics] texture load hook prepare failed result=%d\n", result);
    return;
  }

  result = funchook_install(texture_load_bits_funchook, 0);
  if (result != 0)
  {
    texture_load_bits_hook_failed = true;
    funchook_destroy(texture_load_bits_funchook);
    texture_load_bits_funchook = nullptr;
    texture_load_bits_original = nullptr;
    print("[nographics] texture load hook install failed result=%d\n", result);
    return;
  }

  texture_load_bits_hooked = true;
  print("[nographics] texture load hook enabled\n");
}

void disable_texture_load_hook()
{
  if (texture_load_bits_funchook == nullptr)
  {
    texture_load_bits_hooked = false;
    return;
  }

  if (texture_load_bits_hooked)
  {
    const int result = funchook_uninstall(texture_load_bits_funchook, 0);
    if (result != 0)
    {
      print("[nographics] texture load hook uninstall failed result=%d\n", result);
    }
  }

  funchook_destroy(texture_load_bits_funchook);
  texture_load_bits_funchook = nullptr;
  texture_load_bits_original = nullptr;
  get_scratch_vtf_texture = nullptr;
  handle_file_load_failed_texture = nullptr;
  texture_load_bits_hooked = false;
}

void enable_material_system_render_target_hooks()
{
  if (material_system_render_target_hooked || material_system == nullptr)
  {
    return;
  }

  material_system_vtable = *reinterpret_cast<void***>(material_system);

  bool ok = true;
  ok &= hook_vtable(
    material_system_vtable,
    material_system_create_render_target_texture_index,
    reinterpret_cast<void*>(create_render_target_texture_hook),
    &create_render_target_texture_original);
  ok &= hook_vtable(
    material_system_vtable,
    material_system_create_named_render_target_texture_ex_index,
    reinterpret_cast<void*>(create_named_render_target_texture_ex_hook),
    &create_named_render_target_texture_ex_original);
  ok &= hook_vtable(
    material_system_vtable,
    material_system_create_named_render_target_texture_index,
    reinterpret_cast<void*>(create_named_render_target_texture_hook),
    &create_named_render_target_texture_original);
  ok &= hook_vtable(
    material_system_vtable,
    material_system_create_named_render_target_texture_ex2_index,
    reinterpret_cast<void*>(create_named_render_target_texture_ex2_hook),
    &create_named_render_target_texture_ex2_original);

  if (!ok)
  {
    material_system_render_target_hooked = true;
    disable_material_system_render_target_hooks();
    print("[nographics] material render target hook setup failed\n");
    return;
  }

  material_system_render_target_hooked = true;
}

void disable_material_system_render_target_hooks()
{
  if (!material_system_render_target_hooked)
  {
    return;
  }

  if (create_render_target_texture_original != nullptr)
  {
    write_to_table(material_system_vtable, material_system_create_render_target_texture_index, reinterpret_cast<void*>(create_render_target_texture_original));
  }
  if (create_named_render_target_texture_ex_original != nullptr)
  {
    write_to_table(material_system_vtable, material_system_create_named_render_target_texture_ex_index, reinterpret_cast<void*>(create_named_render_target_texture_ex_original));
  }
  if (create_named_render_target_texture_original != nullptr)
  {
    write_to_table(material_system_vtable, material_system_create_named_render_target_texture_index, reinterpret_cast<void*>(create_named_render_target_texture_original));
  }
  if (create_named_render_target_texture_ex2_original != nullptr)
  {
    write_to_table(material_system_vtable, material_system_create_named_render_target_texture_ex2_index, reinterpret_cast<void*>(create_named_render_target_texture_ex2_original));
  }

  material_system_render_target_hooked = false;
  material_system_vtable = nullptr;
  create_render_target_texture_original = nullptr;
  create_named_render_target_texture_ex_original = nullptr;
  create_named_render_target_texture_original = nullptr;
  create_named_render_target_texture_ex2_original = nullptr;
}

void enable_studio_render_hooks()
{
  if (studio_render_hooked || studio_render_interface == nullptr)
  {
    return;
  }

  studio_render_vtable = *reinterpret_cast<void***>(studio_render_interface);

  bool ok = true;
  ok &= hook_vtable(studio_render_vtable, studio_render_draw_model_index, reinterpret_cast<void*>(studio_render_draw_model_hook), &studio_render_draw_model_original);
  ok &= hook_vtable(studio_render_vtable, studio_render_draw_model_static_prop_index, reinterpret_cast<void*>(studio_render_draw_model_static_prop_hook), &studio_render_draw_model_static_prop_original);
  ok &= hook_vtable(studio_render_vtable, studio_render_draw_static_prop_decals_index, reinterpret_cast<void*>(studio_render_draw_static_prop_decals_hook), &studio_render_draw_static_prop_decals_original);
  ok &= hook_vtable(studio_render_vtable, studio_render_draw_static_prop_shadows_index, reinterpret_cast<void*>(studio_render_draw_static_prop_shadows_hook), &studio_render_draw_static_prop_shadows_original);
  ok &= hook_vtable(studio_render_vtable, studio_render_add_decal_index, reinterpret_cast<void*>(studio_render_add_decal_hook), &studio_render_add_decal_original);
  ok &= hook_vtable(studio_render_vtable, studio_render_add_shadow_index, reinterpret_cast<void*>(studio_render_add_shadow_hook), &studio_render_add_shadow_original);
  ok &= hook_vtable(studio_render_vtable, studio_render_draw_model_array_index, reinterpret_cast<void*>(studio_render_draw_model_array_hook), &studio_render_draw_model_array_original);

  if (!ok)
  {
    studio_render_hooked = true;
    disable_studio_render_hooks();
    print("[nographics] studio render hook setup failed\n");
    return;
  }

  studio_render_hooked = true;
}

void disable_studio_render_hooks()
{
  if (!studio_render_hooked)
  {
    return;
  }

  if (studio_render_draw_model_original != nullptr)
  {
    write_to_table(studio_render_vtable, studio_render_draw_model_index, reinterpret_cast<void*>(studio_render_draw_model_original));
  }
  if (studio_render_draw_model_static_prop_original != nullptr)
  {
    write_to_table(studio_render_vtable, studio_render_draw_model_static_prop_index, reinterpret_cast<void*>(studio_render_draw_model_static_prop_original));
  }
  if (studio_render_draw_static_prop_decals_original != nullptr)
  {
    write_to_table(studio_render_vtable, studio_render_draw_static_prop_decals_index, reinterpret_cast<void*>(studio_render_draw_static_prop_decals_original));
  }
  if (studio_render_draw_static_prop_shadows_original != nullptr)
  {
    write_to_table(studio_render_vtable, studio_render_draw_static_prop_shadows_index, reinterpret_cast<void*>(studio_render_draw_static_prop_shadows_original));
  }
  if (studio_render_add_decal_original != nullptr)
  {
    write_to_table(studio_render_vtable, studio_render_add_decal_index, reinterpret_cast<void*>(studio_render_add_decal_original));
  }
  if (studio_render_add_shadow_original != nullptr)
  {
    write_to_table(studio_render_vtable, studio_render_add_shadow_index, reinterpret_cast<void*>(studio_render_add_shadow_original));
  }
  if (studio_render_draw_model_array_original != nullptr)
  {
    write_to_table(studio_render_vtable, studio_render_draw_model_array_index, reinterpret_cast<void*>(studio_render_draw_model_array_original));
  }

  studio_render_hooked = false;
  studio_render_vtable = nullptr;
  studio_render_draw_model_original = nullptr;
  studio_render_draw_model_static_prop_original = nullptr;
  studio_render_draw_static_prop_decals_original = nullptr;
  studio_render_draw_static_prop_shadows_original = nullptr;
  studio_render_add_decal_original = nullptr;
  studio_render_add_shadow_original = nullptr;
  studio_render_draw_model_array_original = nullptr;
}

void enable_mdl_cache_touch_all_data_hook()
{
  if (mdl_cache_touch_all_data_hooked || mdl_cache == nullptr)
  {
    return;
  }

  mdl_cache_vtable = *reinterpret_cast<void***>(mdl_cache);

  bool ok = true;
  ok &= hook_vtable(
    mdl_cache_vtable,
    mdl_cache_touch_all_data_index,
    reinterpret_cast<void*>(mdl_cache_touch_all_data_hook),
    &mdl_cache_touch_all_data_original);
  ok &= hook_vtable(
    mdl_cache_vtable,
    mdl_cache_touch_all_data_extra_index,
    reinterpret_cast<void*>(mdl_cache_touch_all_data_hook),
    &mdl_cache_touch_all_data_extra_original);

  if (!ok)
  {
    mdl_cache_touch_all_data_hooked = true;
    disable_mdl_cache_touch_all_data_hook();
    print("[nographics] mdl cache touch all data hook setup failed\n");
    return;
  }

  mdl_cache_touch_all_data_hooked = true;
}

void disable_mdl_cache_touch_all_data_hook()
{
  if (!mdl_cache_touch_all_data_hooked)
  {
    return;
  }

  if (mdl_cache_touch_all_data_original != nullptr)
  {
    write_to_table(mdl_cache_vtable, mdl_cache_touch_all_data_index, reinterpret_cast<void*>(mdl_cache_touch_all_data_original));
  }
  if (mdl_cache_touch_all_data_extra_original != nullptr)
  {
    write_to_table(mdl_cache_vtable, mdl_cache_touch_all_data_extra_index, reinterpret_cast<void*>(mdl_cache_touch_all_data_extra_original));
  }

  mdl_cache_touch_all_data_hooked = false;
  mdl_cache_vtable = nullptr;
  mdl_cache_touch_all_data_original = nullptr;
  mdl_cache_touch_all_data_extra_original = nullptr;
}

void enable_file_system_hooks()
{
  if (file_system_hooked || game_file_system == nullptr)
  {
    return;
  }

  file_system_vtable = *reinterpret_cast<void***>(game_file_system);
  auto* base_subobject = reinterpret_cast<std::uint8_t*>(game_file_system) + base_file_system_vptr_offset;
  base_file_system_vtable = *reinterpret_cast<void***>(base_subobject);

  bool ok = true;
  ok &= hook_vtable(file_system_vtable, file_system_find_first_index, reinterpret_cast<void*>(find_first_hook), &find_first_original);
  ok &= hook_vtable(file_system_vtable, file_system_find_next_index, reinterpret_cast<void*>(find_next_hook), &find_next_original);
  ok &= hook_vtable(file_system_vtable, file_system_async_read_multiple_index, reinterpret_cast<void*>(async_read_multiple_hook), &async_read_multiple_original);
  ok &= hook_vtable(file_system_vtable, file_system_open_ex_index, reinterpret_cast<void*>(open_ex_hook), &open_ex_original);
  ok &= hook_vtable(file_system_vtable, file_system_read_file_ex_index, reinterpret_cast<void*>(read_file_ex_hook), &read_file_ex_original);
  ok &= hook_vtable(file_system_vtable, file_system_add_files_to_cache_index, reinterpret_cast<void*>(add_files_to_cache_hook), &add_files_to_cache_original);
  ok &= hook_vtable(base_file_system_vtable, base_file_system_open_index, reinterpret_cast<void*>(open_hook), &open_original);
  ok &= hook_vtable(base_file_system_vtable, base_file_system_precache_index, reinterpret_cast<void*>(precache_hook), &precache_original);
  ok &= hook_vtable(base_file_system_vtable, base_file_system_file_exists_index, reinterpret_cast<void*>(file_exists_hook), &file_exists_original);
  ok &= hook_vtable(base_file_system_vtable, base_file_system_read_file_index, reinterpret_cast<void*>(read_file_hook), &read_file_original);

  if (!ok)
  {
    file_system_hooked = true;
    disable_file_system_hooks();
    print("[nographics] filesystem hook setup failed\n");
    return;
  }

  file_system_hooked = true;
}

void disable_file_system_hooks()
{
  if (!file_system_hooked)
  {
    return;
  }

  if (find_first_original != nullptr) write_to_table(file_system_vtable, file_system_find_first_index, reinterpret_cast<void*>(find_first_original));
  if (find_next_original != nullptr) write_to_table(file_system_vtable, file_system_find_next_index, reinterpret_cast<void*>(find_next_original));
  if (async_read_multiple_original != nullptr) write_to_table(file_system_vtable, file_system_async_read_multiple_index, reinterpret_cast<void*>(async_read_multiple_original));
  if (open_ex_original != nullptr) write_to_table(file_system_vtable, file_system_open_ex_index, reinterpret_cast<void*>(open_ex_original));
  if (read_file_ex_original != nullptr) write_to_table(file_system_vtable, file_system_read_file_ex_index, reinterpret_cast<void*>(read_file_ex_original));
  if (add_files_to_cache_original != nullptr) write_to_table(file_system_vtable, file_system_add_files_to_cache_index, reinterpret_cast<void*>(add_files_to_cache_original));
  if (open_original != nullptr) write_to_table(base_file_system_vtable, base_file_system_open_index, reinterpret_cast<void*>(open_original));
  if (precache_original != nullptr) write_to_table(base_file_system_vtable, base_file_system_precache_index, reinterpret_cast<void*>(precache_original));
  if (file_exists_original != nullptr) write_to_table(base_file_system_vtable, base_file_system_file_exists_index, reinterpret_cast<void*>(file_exists_original));
  if (read_file_original != nullptr) write_to_table(base_file_system_vtable, base_file_system_read_file_index, reinterpret_cast<void*>(read_file_original));

  file_system_hooked = false;

  if (client != nullptr)
  {
    using invalidate_mdl_cache_fn = void (*)(void*);
    auto** vtable = *reinterpret_cast<void***>(client);
    auto invalidate_mdl_cache = reinterpret_cast<invalidate_mdl_cache_fn>(vtable[65]);
    invalidate_mdl_cache(client);
  }
}

void update_material_stub(bool enabled)
{
  if (material_system == nullptr || material_stub_enabled == enabled)
  {
    return;
  }

  material_system->set_in_stub_mode(enabled);
  material_stub_enabled = enabled;
}

void resolve_game_file_system_interface()
{
  if (game_file_system != nullptr)
  {
    return;
  }

  if (module_is_loaded("filesystem_stdio.so"))
  {
    game_file_system = static_cast<file_system*>(get_interface("./bin/linux64/filesystem_stdio.so", "VFileSystem022"));
  }

  if (game_file_system == nullptr && module_is_loaded("filesystem_steam.so"))
  {
    game_file_system = static_cast<file_system*>(get_interface("./bin/linux64/filesystem_steam.so", "VFileSystem022"));
  }

  if (game_file_system != nullptr || !module_is_loaded(client_module_name))
  {
    return;
  }

  auto* match = reinterpret_cast<std::uint8_t*>(sigscan_module(client_module_name, sigs::client_file_system));
  if (match != nullptr)
  {
    game_file_system = *reinterpret_cast<file_system**>(resolve_rip_target(match + 15, 3, 7));
  }
}

void resolve_material_system_interface()
{
  if (material_system != nullptr || !module_is_loaded("materialsystem.so"))
  {
    return;
  }

  material_system = static_cast<MaterialSystem*>(get_interface("./bin/linux64/materialsystem.so", "VMaterialSystem082"));
}

void resolve_studio_render_interface()
{
  if (studio_render_interface != nullptr || !module_is_loaded("studiorender.so"))
  {
    return;
  }

  studio_render_interface = get_interface("./bin/linux64/studiorender.so", "VStudioRender025");
}

void resolve_mdl_cache_interface()
{
  if (mdl_cache != nullptr || !module_is_loaded("datacache.so"))
  {
    return;
  }

  mdl_cache = static_cast<mdl_cache_interface*>(get_interface("./bin/linux64/datacache.so", "MDLCache004"));
}

} // namespace

void initialize()
{
  if constexpr (textmode_build)
  {
    config.misc.exploits.null_graphics = true;
    config.misc.exploits.null_graphics_render_stubs = true;
  }

  resolve_game_file_system_interface();

  if (game_file_system != nullptr)
  {
    initialized = true;
    return;
  }

  if (!initialized && module_is_loaded(client_module_name))
  {
    print("[nographics] VFileSystem022 is missing\n");
    initialized = true;
  }
}

void prepare_startup_patches()
{
  if constexpr (textmode_build)
  {
    if (startup_patch_running.exchange(true, std::memory_order_acq_rel))
    {
      return;
    }
    struct startup_patch_release
    {
      ~startup_patch_release()
      {
        startup_patch_running.store(false, std::memory_order_release);
      }
    } release;

    initialize();
    apply_nographics_convar_overrides();
    resolve_material_system_interface();
    resolve_studio_render_interface();
    resolve_mdl_cache_interface();
    enable_texture_load_hook();
    enable_file_system_hooks();
    enable_material_system_render_target_hooks();
    enable_studio_render_hooks();
    enable_mdl_cache_touch_all_data_hook();
    enable_bone_setup_attachment_matrices_guard();
    update_material_stub(true);
    apply_render_patches();
  }
}

void prepare_render_patches()
{
  if constexpr (textmode_build)
  {
    prepare_startup_patches();
  }
}

void on_library_loaded(const char* library_path)
{
  if constexpr (textmode_build)
  {
    if (is_startup_patch_module(library_path))
    {
      prepare_startup_patches();
    }
  }
  else
  {
    (void)library_path;
  }
}

void update()
{
  sync_nographics_toggles();
  initialize();

  const bool enabled = textmode_build || config.misc.exploits.null_graphics;
  if (enabled)
  {
    apply_nographics_convar_overrides();
    resolve_material_system_interface();
    resolve_studio_render_interface();
    resolve_mdl_cache_interface();
    enable_texture_load_hook();
    enable_file_system_hooks();
    enable_material_system_render_target_hooks();
    enable_studio_render_hooks();
    enable_mdl_cache_touch_all_data_hook();
    enable_bone_setup_attachment_matrices_guard();
    update_material_stub(true);
    if (textmode_build || config.misc.exploits.null_graphics_render_stubs)
    {
      apply_render_patches();
    }
    else
    {
      restore_render_patches();
    }
    return;
  }

  restore_nographics_convar_overrides();
  restore_render_patches();
  update_material_stub(false);
  disable_studio_render_hooks();
  disable_material_system_render_target_hooks();
  disable_mdl_cache_touch_all_data_hook();
  disable_texture_load_hook();
  disable_file_system_hooks();
  disable_bone_setup_attachment_matrices_guard();
}

void shutdown()
{
  restore_nographics_convar_overrides();
  restore_render_patches();
  update_material_stub(false);
  disable_studio_render_hooks();
  disable_material_system_render_target_hooks();
  disable_mdl_cache_touch_all_data_hook();
  disable_texture_load_hook();
  disable_file_system_hooks();
  disable_bone_setup_attachment_matrices_guard();
}

bool is_enabled()
{
  return textmode_build || config.misc.exploits.null_graphics;
}

bool should_skip_rendering_hooks()
{
  return textmode_build || is_noshaderapi() ||
         (config.misc.exploits.null_graphics && config.misc.exploits.null_graphics_render_stubs);
}

bool is_noshaderapi()
{
  static const bool from_command_line = command_line_has_noshaderapi();
  if (from_command_line)
  {
    return true;
  }

  static int from_modules = -1;
  if (from_modules < 0 && (material_system != nullptr || module_is_loaded("materialsystem.so")))
  {
    from_modules = empty_shader_api_is_active() ? 1 : 0;
  }

  return from_modules == 1;
}

const char* redirect_shaderapi_path(const char* library_path)
{
  if (library_path == nullptr || !is_noshaderapi())
  {
    return library_path;
  }

  const std::string_view path{ library_path };
  if (!is_shaderapivk_path(path))
  {
    return library_path;
  }

  thread_local std::string redirected_path{};
  const auto slash = path.find_last_of('/');
  if (slash == std::string_view::npos)
  {
    redirected_path = "shaderapiempty.so";
  }
  else
  {
    redirected_path.assign(path.substr(0, slash + 1));
    redirected_path += "shaderapiempty.so";
  }

  return redirected_path.c_str();
}

} // namespace nographics

#if defined(__linux__)
extern "C" __attribute__((visibility("default"))) SDL_Window* SDL_CreateWindow(
  const char* title,
  int x,
  int y,
  int w,
  int h,
  unsigned int flags)
{
  using sdl_create_window_fn = SDL_Window* (*)(const char*, int, int, int, int, unsigned int);
  static sdl_create_window_fn sdl_create_window_original =
    reinterpret_cast<sdl_create_window_fn>(dlsym(RTLD_NEXT, "SDL_CreateWindow"));

  if (sdl_create_window_original == nullptr)
  {
    return nullptr;
  }

  constexpr unsigned int sdl_window_opengl = 0x00000002u;
  constexpr unsigned int sdl_window_hidden = 0x00000008u;
  constexpr unsigned int sdl_window_vulkan = 0x10000000u;

  unsigned int fixed_flags = flags;
  if (nographics::is_noshaderapi())
  {
    fixed_flags &= ~(sdl_window_opengl | sdl_window_vulkan);
    fixed_flags |= sdl_window_hidden;
  }

  return sdl_create_window_original(title, x, y, w, h, fixed_flags);
}
#endif
