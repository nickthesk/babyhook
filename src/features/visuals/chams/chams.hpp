/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/features/visuals/chams/chams.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef CHAMS_HPP
#define CHAMS_HPP

#include <cassert>
#include <vector>

#include "core/assert.hpp"

#include "features/menu/config.hpp"
#include "features/visuals/groups/visual_groups.hpp"

#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/material_system.hpp"
#include "games/tf2/sdk/interfaces/model_render.hpp"
#include "games/tf2/sdk/interfaces/render_view.hpp"

#include "games/tf2/sdk/materials/material.hpp"
#include "games/tf2/sdk/materials/material_var.hpp"


struct ModelRenderInfo;
void (*draw_model_execute_original)(void*, void*, ModelRenderInfo*, VMatrix*);

#define DME_RETURN { draw_model_execute_original(me, state, pinfo, bone_to_world); return; }

inline static std::vector<Material*> materials;

struct chams_settings {
  RGBA_float color{};
  RGBA_float color_z{};
  RGBA_float color_overlay{};
  RGBA_float color_z_overlay{};
  bool ignore_z = false;
  bool ignore_z_overlay = false;
  bool wireframe = false;
  bool wireframe_z = false;
  bool wireframe_overlay = false;
  bool wireframe_z_overlay = false;
  Material* material = nullptr;
  Material* material_z = nullptr;
  Material* material_overlay = nullptr;
  Material* material_z_overlay = nullptr;
};

static Material* create_material_ex(const char* name, const char* shader, const char* material_source) {
  if (material_system == nullptr || key_values_constructor_original == nullptr || key_values_load_from_buffer_original == nullptr) {
    return nullptr;
  }

  KeyValues* values = new KeyValues(shader);
  if (!values->load_from_buffer(name, material_source)) {
    delete values;
    return nullptr;
  }

  Material* material = material_system->create_material(name, values);
  if (material != nullptr) {
    material->increment_reference_count();
  }

  return material;
}

static void initialize_materials() {
  if (!materials.empty()) {
    return;
  }

  materials.reserve(6);
  materials.emplace_back(nullptr);
  
  materials.emplace_back(create_material_ex("CATHOOK_chams_flat_v2",
                                            "UnlitGeneric",
                                            R"#(
"UnlitGeneric" {
   "$basetexture" "vgui/white_additive"
   "$model" "1"
   "$nocull" "1"
   "$nofog" "1"
}
)#"));
  
  materials.emplace_back(create_material_ex("CATHOOK_chams_shaded_v2",
                                            "VertexLitGeneric",
                                            R"#(
"VertexLitGeneric" {
   "$basetexture" "vgui/white_additive"
   "$model" "1"
   "$nocull" "1"
   "$nofog" "1"
   "$halflambert" "1"
}
)#"));

  materials.emplace_back(create_material_ex("CATHOOK_chams_fresnel_v2",
                                            "VertexLitGeneric",
                                            R"#(
"VertexLitGeneric" {
   "$basetexture" "vgui/white_additive"
   "$model" "1"
   "$nocull" "1"
   "$nofog" "1"
   "$bumpmap" "models/player/shared/shared_normal"
   "$additive" "1"
   "$phong" "1"
   "$phongboost" "6"
   "$phongexponent" "12"
   "$phongfresnelranges" "[0 0.5 1]"
   "$envmap" "env_cubemap"
   "$envmapfresnel" "1"
   "$envmaptint" "[1 1 1]"
}
)#"));

  materials.emplace_back(create_material_ex("CATHOOK_chams_glossy_v2",
                                            "VertexLitGeneric",
                                            R"#(
"VertexLitGeneric" {
   "$basetexture" "vgui/white_additive"
   "$model" "1"
   "$nocull" "1"
   "$nofog" "1"
   "$bumpmap" "water/tfwater001_normal"
   "$lightwarptexture" "models/player/pyro/pyro_lightwarp"
   "$halflambert" "1"
   "$phong" "1"
   "$phongexponent" "25"
   "$phongboost" "2"
   "$phongfresnelranges" "[0 3 15]"
   "$envmap" "env_cubemap"
   "$envmapfresnel" "1"
   "$envmapfresnelminmaxexp" "[0.01 1 2]"
   "$normalmapalphaenvmapmask" "1"
   "$selfillum" "1"
   "$envmaptint" "[1 1 1]"
   "$rimlight" "1"
   "$rimlightexponent" "4"
   "$rimlightboost" "2"
}
)#"));

  materials.emplace_back(create_material_ex("CATHOOK_chams_additive_v2",
                                            "UnlitGeneric",
                                            R"#(
"UnlitGeneric" {
   "$basetexture" "vgui/white_additive"
   "$model" "1"
   "$nocull" "1"
   "$nofog" "1"
   "$additive" "1"
   "$selfillum" "1"
   "$selfillumtint" "[1 1 1]"
}
)#"));
}

static void set_material_vec_if_found(Material* material, const char* var_name, RGBA_float color) {
  bool found = false;
  auto* material_var = material->find_var(var_name, &found);
  if (found && material_var != nullptr) {
    material_var->set_vec_value(color);
  }
}

static void set_material_information(Material* material, RGBA_float color, bool wireframe = false, bool ignore_z = false, OverrideType override_type = OVERRIDE_NORMAL) {
  error_assert(material == nullptr, "Material is null in set_material_color_flags()!");
  
  render_view->set_color_modulation(&color);
  render_view->set_blend(color.a);
  material->color_modulate(color);
  material->alpha_modulate(color.a);

  set_material_vec_if_found(material, "$envmaptint", color);
  set_material_vec_if_found(material, "$selfillumtint", color);
  
  material->set_material_flag(MATERIAL_VAR_IGNOREZ, ignore_z);
  material->set_material_flag(MATERIAL_VAR_WIREFRAME, wireframe);
  model_render->forced_material_override(material, override_type);
}

[[nodiscard]] static size_t get_material_index(chams_material_type type) {
  switch (type) {
    case chams_material_type::none:
      return 0;
    case chams_material_type::flat:
    case chams_material_type::flat_wireframe:
      return 1;
    case chams_material_type::shaded:
    case chams_material_type::shaded_wireframe:
      return 2;
    case chams_material_type::fresnel:
    case chams_material_type::fresnel_wireframe:
      return 3;
    case chams_material_type::glossy:
    case chams_material_type::glossy_wireframe:
      return 4;
    case chams_material_type::additive:
    case chams_material_type::additive_wireframe:
      return 5;
    default:
      return 0;
  }
}

[[nodiscard]] static bool is_wireframe_material(chams_material_type type) {
  switch (type) {
    case chams_material_type::flat_wireframe:
    case chams_material_type::shaded_wireframe:
    case chams_material_type::fresnel_wireframe:
    case chams_material_type::glossy_wireframe:
    case chams_material_type::additive_wireframe:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] static Material* get_material(chams_material_type type) {
  const auto material_index = get_material_index(type);
  if (material_index >= materials.size()) {
    return nullptr;
  }

  return materials[material_index];
}

[[nodiscard]] static chams_settings get_chams_settings(const visual_group& group) {
  auto settings = chams_settings{};
  settings.color = group.chams.visible_override_color ? group.chams.visible_color : group.color;
  settings.color_z = group.chams.occluded_override_color ? group.chams.occluded_color : group.color;
  settings.ignore_z = group.chams.ignore_z;
  settings.wireframe = is_wireframe_material(group.chams.visible_material);
  settings.wireframe_z = is_wireframe_material(group.chams.occluded_material);
  settings.material = get_material(group.chams.visible_material);
  settings.material_z = get_material(group.chams.occluded_material);
  return settings;
}

static void run_chams_pass(void* me, void* state, ModelRenderInfo* pinfo, VMatrix* bone_to_world, RenderContext* render_context, Material* material, Material* material_z, RGBA_float color, RGBA_float color_z, bool ignore_z, bool wireframe, bool wireframe_z, bool render_original_when_material_missing) {
  if (material == nullptr && material_z == nullptr) {
    if (render_original_when_material_missing) {
      draw_model_execute_original(me, state, pinfo, bone_to_world);
    }
    return;
  }

  if (ignore_z && material_z != nullptr) {
    render_context->set_depth_range(0, 0.2);
    set_material_information(material_z, color_z, wireframe_z, true);
    draw_model_execute_original(me, state, pinfo, bone_to_world);
    render_context->set_depth_range(0, 1);
  }

  if (material != nullptr) {
    set_material_information(material, color, wireframe, false, OVERRIDE_NORMAL);
    draw_model_execute_original(me, state, pinfo, bone_to_world);
  } else if (render_original_when_material_missing) {
    draw_model_execute_original(me, state, pinfo, bone_to_world);
  }
}

static void apply_chams_settings(void* me, void* state, ModelRenderInfo* pinfo, VMatrix* bone_to_world, const chams_settings& settings, bool render_original_when_material_missing = true) {
  auto* render_context = material_system->get_render_context();
  if (render_context == nullptr) {
    if (render_original_when_material_missing) {
      draw_model_execute_original(me, state, pinfo, bone_to_world);
    }
    return;
  }

  if (settings.material == nullptr && settings.material_z == nullptr &&
      settings.material_overlay == nullptr && settings.material_z_overlay == nullptr) {
    if (render_original_when_material_missing) {
      draw_model_execute_original(me, state, pinfo, bone_to_world);
    }
    return;
  }

  run_chams_pass(me, state, pinfo, bone_to_world, render_context, settings.material, settings.material_z, settings.color, settings.color_z, settings.ignore_z, settings.wireframe, settings.wireframe_z, render_original_when_material_missing);
  run_chams_pass(me, state, pinfo, bone_to_world, render_context, settings.material_overlay, settings.material_z_overlay, settings.color_overlay, settings.color_z_overlay, settings.ignore_z_overlay, settings.wireframe_overlay, settings.wireframe_z_overlay, false);
}

#endif
