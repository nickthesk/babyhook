/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/visuals/overlay_projection.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef OVERLAY_PROJECTION_HPP
#define OVERLAY_PROJECTION_HPP

#include "imgui/dearimgui.hpp"

#include "core/types.hpp"

#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/render_view.hpp"
#include "games/tf2/sdk/interfaces/surface.hpp"

namespace overlay_projection
{

enum class screen_space_t
{
  imgui,
  surface,
  engine,
};

struct state_t
{
  bool valid = false;
  bool matrix_valid = false;
  VMatrix world_to_projection{};
  float screen_width = 0.0f;
  float screen_height = 0.0f;
};

inline state_t state{};

inline void use_imgui_size(float* width, float* height)
{
  if (width == nullptr || height == nullptr || (*width > 0.0f && *height > 0.0f)) {
    return;
  }

  const auto& io = ImGui::GetIO();
  if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f) {
    *width = io.DisplaySize.x;
    *height = io.DisplaySize.y;
  }
}

inline void use_surface_size(float* width, float* height)
{
  if (width == nullptr || height == nullptr || (*width > 0.0f && *height > 0.0f) || surface == nullptr) {
    return;
  }

  const auto size = surface->get_screen_size();
  if (size.x > 0 && size.y > 0) {
    *width = static_cast<float>(size.x);
    *height = static_cast<float>(size.y);
  }
}

inline void use_engine_size(float* width, float* height)
{
  if (width == nullptr || height == nullptr || (*width > 0.0f && *height > 0.0f) || engine == nullptr) {
    return;
  }

  const auto size = engine->get_screen_size();
  if (size.x > 0 && size.y > 0) {
    *width = static_cast<float>(size.x);
    *height = static_cast<float>(size.y);
  }
}

inline void use_view_size(const view_setup& view, float* width, float* height)
{
  if (width == nullptr || height == nullptr || (*width > 0.0f && *height > 0.0f)) {
    return;
  }

  if (view.width > 0 && view.height > 0) {
    *width = static_cast<float>(view.width);
    *height = static_cast<float>(view.height);
  }
}

[[nodiscard]] inline bool begin_frame(screen_space_t screen_space = screen_space_t::imgui)
{
  state.valid = false;
  state.screen_width = 0.0f;
  state.screen_height = 0.0f;

  if (client == nullptr || engine == nullptr || render_view == nullptr) {
    state.matrix_valid = false;
    return false;
  }

  auto matrix_view = view_setup{};
  if (!client->get_player_view(matrix_view)) {
    state.matrix_valid = false;
    return false;
  }

  VMatrix world_to_screen{};
  VMatrix view_to_projection{};
  VMatrix world_to_projection{};
  VMatrix world_to_pixels{};
  render_view->get_matrices_for_view(
    matrix_view,
    &world_to_screen,
    &view_to_projection,
    &world_to_projection,
    &world_to_pixels);
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      state.world_to_projection[row][column] = world_to_projection[row][column];
    }
  }

  state.matrix_valid = true;

  auto local_view = view_setup{};
  if (!client->get_player_view(local_view)) {
    state.matrix_valid = false;
    return false;
  }

  auto screen_width = 0.0f;
  auto screen_height = 0.0f;

  if (screen_space == screen_space_t::surface) {
    use_surface_size(&screen_width, &screen_height);
    use_engine_size(&screen_width, &screen_height);
    use_view_size(local_view, &screen_width, &screen_height);
    use_imgui_size(&screen_width, &screen_height);
  } else if (screen_space == screen_space_t::engine) {
    use_engine_size(&screen_width, &screen_height);
    use_view_size(local_view, &screen_width, &screen_height);
    use_imgui_size(&screen_width, &screen_height);
  } else {
    use_imgui_size(&screen_width, &screen_height);
    use_engine_size(&screen_width, &screen_height);
    use_view_size(local_view, &screen_width, &screen_height);
  }

  if (screen_width <= 0.0f || screen_height <= 0.0f) {
    return false;
  }

  state.screen_width = screen_width;
  state.screen_height = screen_height;
  state.valid = true;
  return true;
}

[[nodiscard]] inline bool update_view_matrix()
{
  if (client == nullptr || render_view == nullptr) {
    state.matrix_valid = false;
    return false;
  }

  auto local_view = view_setup{};
  if (!client->get_player_view(local_view)) {
    state.matrix_valid = false;
    return false;
  }

  VMatrix world_to_screen{};
  VMatrix view_to_projection{};
  VMatrix world_to_projection{};
  VMatrix world_to_pixels{};
  render_view->get_matrices_for_view(
    local_view,
    &world_to_screen,
    &view_to_projection,
    &world_to_projection,
    &world_to_pixels);
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      state.world_to_projection[row][column] = world_to_projection[row][column];
    }
  }

  state.matrix_valid = true;
  return true;
}

[[nodiscard]] inline bool world_to_screen(const Vec3& point, Vec3* screen)
{
  if (screen == nullptr || !state.valid) {
    return false;
  }

  const auto& matrix = state.world_to_projection;
  const auto w = (matrix[3][0] * point.x) + (matrix[3][1] * point.y) + (matrix[3][2] * point.z) + matrix[3][3];
  screen->z = 0.0f;
  if (w <= 0.001f) {
    return false;
  }

  const auto inv_w = 1.0f / w;
  const auto projected_x = ((matrix[0][0] * point.x) + (matrix[0][1] * point.y) + (matrix[0][2] * point.z) + matrix[0][3]) * inv_w;
  const auto projected_y = ((matrix[1][0] * point.x) + (matrix[1][1] * point.y) + (matrix[1][2] * point.z) + matrix[1][3]) * inv_w;

  screen->x = (state.screen_width * 0.5f) + (projected_x * state.screen_width * 0.5f) + 0.5f;
  screen->y = (state.screen_height * 0.5f) - (projected_y * state.screen_height * 0.5f) + 0.5f;
  return true;
}

} // namespace overlay_projection

#endif
