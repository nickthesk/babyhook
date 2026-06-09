/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/vulkan.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "imgui/dearimgui.hpp"
#include "imgui/imgui_impl_vulkan.h"

#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/surface.hpp"

#include "features/combat/backtrack/backtrack.hpp"
#include "features/visuals/esp/esp.cpp"
#include "features/visuals/hitmarker.hpp"
#include "features/visuals/spectator_list.hpp"

#include "core/print.hpp"

#include "features/menu/menu.hpp"
#include "features/menu/indicators.hpp"
#include "features/automation/navbot/navbot_controller.hpp"
#include "features/automation/nographics/nographics.hpp"

static constexpr uint32_t max_swapchain_images = 8;
static constexpr uint32_t max_present_wait_semaphores = 16;
static constexpr uint32_t invalid_queue_family = static_cast<uint32_t>(-1);

static VkDevice vk_device = VK_NULL_HANDLE;
static const VkAllocationCallbacks* vk_allocator = nullptr;
static std::unique_ptr<VkQueueFamilyProperties[]> queue_families;
static uint32_t queue_family = invalid_queue_family;
static VkRenderPass vk_render_pass = VK_NULL_HANDLE;
static VkDescriptorPool vk_descriptor_pool = VK_NULL_HANDLE;
static VkExtent2D vk_image_extent = {};
static VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
static VkInstance vk_instance = VK_NULL_HANDLE;
static VkPipelineCache vk_pipeline_cache = VK_NULL_HANDLE;
static uint32_t min_image_count = 2;
static uint32_t swapchain_image_count = 0;
static uint32_t count = 0;
static VkSwapchainKHR active_swapchain = VK_NULL_HANDLE;
static VkFormat active_swapchain_format = VK_FORMAT_UNDEFINED;
static uint32_t active_queue_family = invalid_queue_family;
static bool vulkan_renderer_initialized = false;
static bool vulkan_platform_initialized = false;
static bool warned_late_swapchain_format = false;
static bool logged_swapchain_resources = false;
static bool logged_first_overlay_submit = false;
static bool logged_overlay_fence_timeout = false;
static std::atomic_bool vulkan_overlay_disabled = false;

static constexpr auto overlay_fence_wait_timeout = std::chrono::milliseconds(2);

static ImGui_ImplVulkanH_Frame frames[max_swapchain_images] = {};
static ImGui_ImplVulkanH_FrameSemaphores frame_semaphores[max_swapchain_images] = {};

struct queue_family_record
{
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t family = invalid_queue_family;
};

struct render_queue_selection
{
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t family = invalid_queue_family;
};

static std::vector<queue_family_record> queue_family_records;

VkResult (*queue_present_original)(VkQueue, const VkPresentInfoKHR*) = nullptr;
VkResult (*create_swapchain_original)(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*) = nullptr;
void (*destroy_swapchain_original)(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*) = nullptr;
VkResult (*acquire_next_image_original)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*) = nullptr;
VkResult (*acquire_next_image2_original)(VkDevice, const VkAcquireNextImageInfoKHR*, uint32_t*) = nullptr;
VkResult (*create_device_original)(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*) = nullptr;
void (*get_device_queue_original)(VkDevice, uint32_t, uint32_t, VkQueue*) = nullptr;
void (*get_device_queue2_original)(VkDevice, const VkDeviceQueueInfo2*, VkQueue*) = nullptr;

static void remember_queue_family(VkQueue queue, uint32_t family)
{
  if (queue == VK_NULL_HANDLE || family == invalid_queue_family) {
    return;
  }

  for (auto& record : queue_family_records) {
    if (record.queue == queue) {
      record.family = family;
      return;
    }
  }

  queue_family_records.push_back({queue, family});
}

static uint32_t find_queue_family(VkQueue queue)
{
  for (const auto& record : queue_family_records) {
    if (record.queue == queue) {
      return record.family;
    }
  }

  return queue_family;
}

static bool queue_family_supports_graphics(uint32_t family)
{
  return family != invalid_queue_family &&
         family < count &&
         queue_families != nullptr &&
         (queue_families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
}

static render_queue_selection select_render_queue(VkQueue present_queue)
{
  const auto present_family = find_queue_family(present_queue);
  if (queue_family_supports_graphics(present_family)) {
    return {present_queue, present_family};
  }

  for (const auto& record : queue_family_records) {
    if (queue_family_supports_graphics(record.family)) {
      return {record.queue, record.family};
    }
  }

  return {};
}

static VkExtent2D get_overlay_extent()
{
  if (vk_image_extent.width != 0 && vk_image_extent.height != 0) {
    return vk_image_extent;
  }

  if (engine != nullptr) {
    const auto resolution = engine->get_screen_size();
    if (resolution.x > 0 && resolution.y > 0) {
      return VkExtent2D{
        static_cast<uint32_t>(resolution.x),
        static_cast<uint32_t>(resolution.y)
      };
    }
  }

  return VkExtent2D{1, 1};
}

static void update_imgui_overlay_size()
{
  const auto extent = get_overlay_extent();
  auto& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

static void shutdown_vulkan_renderer_backend()
{
  if (ImGui::GetCurrentContext() == nullptr) {
    vulkan_renderer_initialized = false;
    return;
  }

  auto& io = ImGui::GetIO();
  if (io.BackendRendererUserData != nullptr) {
    ImGui_ImplVulkan_Shutdown();
  }

  vulkan_renderer_initialized = false;
}

static void destroy_frame_resources()
{
  if (vk_device == VK_NULL_HANDLE) {
    return;
  }

  for (uint32_t index = 0; index < max_swapchain_images; ++index) {
    if (frames[index].Fence != VK_NULL_HANDLE) {
      vkDestroyFence(vk_device, frames[index].Fence, vk_allocator);
      frames[index].Fence = VK_NULL_HANDLE;
    }

    if (frames[index].CommandBuffer != VK_NULL_HANDLE && frames[index].CommandPool != VK_NULL_HANDLE) {
      vkFreeCommandBuffers(vk_device, frames[index].CommandPool, 1, &frames[index].CommandBuffer);
      frames[index].CommandBuffer = VK_NULL_HANDLE;
    }

    if (frames[index].CommandPool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(vk_device, frames[index].CommandPool, vk_allocator);
      frames[index].CommandPool = VK_NULL_HANDLE;
    }

    if (frames[index].Framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(vk_device, frames[index].Framebuffer, vk_allocator);
      frames[index].Framebuffer = VK_NULL_HANDLE;
    }

    if (frames[index].BackbufferView != VK_NULL_HANDLE) {
      vkDestroyImageView(vk_device, frames[index].BackbufferView, vk_allocator);
      frames[index].BackbufferView = VK_NULL_HANDLE;
    }

    frames[index].Backbuffer = VK_NULL_HANDLE;

    if (frame_semaphores[index].ImageAcquiredSemaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(vk_device, frame_semaphores[index].ImageAcquiredSemaphore, vk_allocator);
      frame_semaphores[index].ImageAcquiredSemaphore = VK_NULL_HANDLE;
    }

    if (frame_semaphores[index].RenderCompleteSemaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(vk_device, frame_semaphores[index].RenderCompleteSemaphore, vk_allocator);
      frame_semaphores[index].RenderCompleteSemaphore = VK_NULL_HANDLE;
    }
  }
}

static void destroy_swapchain_resources(bool wait_idle)
{
  if (vk_device == VK_NULL_HANDLE) {
    return;
  }

  if (wait_idle) {
    vkDeviceWaitIdle(vk_device);
  }

  shutdown_vulkan_renderer_backend();
  destroy_frame_resources();

  if (vk_render_pass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(vk_device, vk_render_pass, vk_allocator);
    vk_render_pass = VK_NULL_HANDLE;
  }

  active_swapchain = VK_NULL_HANDLE;
  active_swapchain_format = VK_FORMAT_UNDEFINED;
  active_queue_family = invalid_queue_family;
  swapchain_image_count = 0;
}

static void shutdown_vulkan_runtime_state(bool release_graphics_resources)
{
  vulkan_overlay_disabled.store(true, std::memory_order_release);

  if (!release_graphics_resources) {
    vulkan_renderer_initialized = false;
    vulkan_platform_initialized = false;
    return;
  }

  destroy_swapchain_resources(true);

  if (vk_descriptor_pool != VK_NULL_HANDLE && vk_device != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(vk_device, vk_descriptor_pool, vk_allocator);
    vk_descriptor_pool = VK_NULL_HANDLE;
  }

  if (ImGui::GetCurrentContext() != nullptr) {
    auto& io = ImGui::GetIO();
    if (vulkan_platform_initialized && io.BackendPlatformUserData != nullptr) {
      ImGui_ImplSDL2_Shutdown();
    }
  }

  vulkan_platform_initialized = false;
  queue_family_records.clear();
  queue_families.reset();

  if (vk_instance != VK_NULL_HANDLE) {
    vkDestroyInstance(vk_instance, vk_allocator);
    vk_instance = VK_NULL_HANDLE;
  }

  vk_device = VK_NULL_HANDLE;
  vk_physical_device = VK_NULL_HANDLE;
  vk_pipeline_cache = VK_NULL_HANDLE;
  vk_image_extent = {};
  queue_family = invalid_queue_family;
  min_image_count = 2;
  count = 0;
}

static bool ensure_descriptor_pool()
{
  if (vk_descriptor_pool != VK_NULL_HANDLE) {
    return true;
  }

  constexpr VkDescriptorPoolSize pool_sizes[] = {
    {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
  };

  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000 * static_cast<uint32_t>(std::size(pool_sizes));
  pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
  pool_info.pPoolSizes = pool_sizes;

  const auto result = vkCreateDescriptorPool(vk_device, &pool_info, vk_allocator, &vk_descriptor_pool);
  if (result != VK_SUCCESS) {
    print("vkCreateDescriptorPool failed: %d\n", result);
    return false;
  }

  return true;
}

static bool create_overlay_render_pass()
{
  if (vk_render_pass != VK_NULL_HANDLE) {
    return true;
  }

  VkAttachmentDescription attachment = {};
  attachment.format = active_swapchain_format;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color_attachment = {};
  color_attachment.attachment = 0;
  color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment;

  std::array<VkSubpassDependency, 2> dependencies = {};
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 1;
  info.pAttachments = &attachment;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = static_cast<uint32_t>(dependencies.size());
  info.pDependencies = dependencies.data();

  const auto result = vkCreateRenderPass(vk_device, &info, vk_allocator, &vk_render_pass);
  if (result != VK_SUCCESS) {
    print("vkCreateRenderPass failed: %d\n", result);
    return false;
  }

  return true;
}

static bool create_frame_resources(VkSwapchainKHR swapchain, uint32_t render_queue_family)
{
  uint32_t image_count = 0;
  auto result = vkGetSwapchainImagesKHR(vk_device, swapchain, &image_count, nullptr);
  if (result != VK_SUCCESS || image_count == 0 || image_count > max_swapchain_images) {
    print("vkGetSwapchainImagesKHR count failed: %d images=%u\n", result, image_count);
    return false;
  }

  std::array<VkImage, max_swapchain_images> backbuffers = {};
  result = vkGetSwapchainImagesKHR(vk_device, swapchain, &image_count, backbuffers.data());
  if (result != VK_SUCCESS) {
    print("vkGetSwapchainImagesKHR images failed: %d\n", result);
    return false;
  }

  const auto extent = get_overlay_extent();

  for (uint32_t index = 0; index < image_count; ++index) {
    auto& frame = frames[index];
    auto& semaphores = frame_semaphores[index];
    frame.Backbuffer = backbuffers[index];

    VkCommandPoolCreateInfo command_pool_info = {};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_info.queueFamilyIndex = render_queue_family;

    result = vkCreateCommandPool(vk_device, &command_pool_info, vk_allocator, &frame.CommandPool);
    if (result != VK_SUCCESS) {
      print("vkCreateCommandPool failed: %d\n", result);
      return false;
    }

    VkCommandBufferAllocateInfo command_buffer_info = {};
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.commandPool = frame.CommandPool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    result = vkAllocateCommandBuffers(vk_device, &command_buffer_info, &frame.CommandBuffer);
    if (result != VK_SUCCESS) {
      print("vkAllocateCommandBuffers failed: %d\n", result);
      return false;
    }

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    result = vkCreateFence(vk_device, &fence_info, vk_allocator, &frame.Fence);
    if (result != VK_SUCCESS) {
      print("vkCreateFence failed: %d\n", result);
      return false;
    }

    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    result = vkCreateSemaphore(vk_device, &semaphore_info, vk_allocator, &semaphores.RenderCompleteSemaphore);
    if (result != VK_SUCCESS) {
      print("vkCreateSemaphore failed: %d\n", result);
      return false;
    }

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = frame.Backbuffer;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = active_swapchain_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    result = vkCreateImageView(vk_device, &view_info, vk_allocator, &frame.BackbufferView);
    if (result != VK_SUCCESS) {
      print("vkCreateImageView failed: %d\n", result);
      return false;
    }

    VkImageView attachment = frame.BackbufferView;
    VkFramebufferCreateInfo framebuffer_info = {};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = vk_render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &attachment;
    framebuffer_info.width = extent.width;
    framebuffer_info.height = extent.height;
    framebuffer_info.layers = 1;

    result = vkCreateFramebuffer(vk_device, &framebuffer_info, vk_allocator, &frame.Framebuffer);
    if (result != VK_SUCCESS) {
      print("vkCreateFramebuffer failed: %d\n", result);
      return false;
    }
  }

  swapchain_image_count = image_count;
  active_queue_family = render_queue_family;

  if (!logged_swapchain_resources) {
    print("Vulkan overlay resources ready: format=%u extent=%ux%u images=%u queue_family=%u\n",
      static_cast<unsigned int>(active_swapchain_format),
      extent.width,
      extent.height,
      image_count,
      render_queue_family);
    logged_swapchain_resources = true;
  }

  return true;
}

static bool ensure_imgui_context()
{
  if (sdl_window == nullptr) {
    return false;
  }

  if (ImGui::GetCurrentContext() == nullptr) {
    ImGui::CreateContext();
  }

  auto& io = ImGui::GetIO();
  io.ConfigWindowsMoveFromTitleBarOnly = true;

  if (!vulkan_platform_initialized && io.BackendPlatformUserData == nullptr) {
    if (!ImGui_ImplSDL2_InitForVulkan(sdl_window)) {
      print("ImGui_ImplSDL2_InitForVulkan failed\n");
      return false;
    }

    vulkan_platform_initialized = true;
  }

  return true;
}

static bool ensure_vulkan_renderer(const render_queue_selection& render_queue)
{
  if (vulkan_renderer_initialized) {
    return true;
  }

  if (!ensure_descriptor_pool() || vk_render_pass == VK_NULL_HANDLE) {
    return false;
  }

  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = vk_instance;
  init_info.PhysicalDevice = vk_physical_device;
  init_info.Device = vk_device;
  init_info.QueueFamily = render_queue.family;
  init_info.Queue = render_queue.queue;
  init_info.PipelineCache = vk_pipeline_cache;
  init_info.DescriptorPool = vk_descriptor_pool;
  init_info.RenderPass = vk_render_pass;
  init_info.MinImageCount = min_image_count;
  init_info.ImageCount = swapchain_image_count;
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init_info.Allocator = vk_allocator;
  init_info.Subpass = 0;

  update_imgui_overlay_size();

  if (!ImGui_ImplVulkan_Init(&init_info)) {
    print("ImGui_ImplVulkan_Init failed\n");
    return false;
  }

  vulkan_renderer_initialized = true;
  orig_style = ImGui::GetStyle();
  set_imgui_theme();
  return true;
}

static bool ensure_swapchain_resources(VkSwapchainKHR swapchain, const render_queue_selection& render_queue)
{
  if (vk_device == VK_NULL_HANDLE || swapchain == VK_NULL_HANDLE) {
    return false;
  }

  if (active_swapchain_format == VK_FORMAT_UNDEFINED) {
    active_swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
    if (!warned_late_swapchain_format) {
      print("Vulkan swapchain was created before hook install; using B8G8R8A8_UNORM fallback\n");
      warned_late_swapchain_format = true;
    }
  }

  if (render_queue.queue == VK_NULL_HANDLE || !queue_family_supports_graphics(render_queue.family)) {
    print("No Vulkan queue family is available for overlay rendering\n");
    return false;
  }

  if (active_swapchain == swapchain && swapchain_image_count != 0 && active_queue_family == render_queue.family) {
    return true;
  }

  const auto swapchain_format = active_swapchain_format;
  destroy_swapchain_resources(true);
  active_swapchain_format = swapchain_format;

  if (!create_overlay_render_pass() || !create_frame_resources(swapchain, render_queue.family)) {
    destroy_swapchain_resources(true);
    return false;
  }

  active_swapchain = swapchain;
  return true;
}

static void draw_imgui_overlay()
{
  if (ImGui::IsKeyPressed(ImGuiKey_Insert, false) || ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
    menu_focused = !menu_focused;
    if (surface != nullptr) {
      surface->set_cursor_visible(menu_focused);
    }
  }

  cat_menu::ensure_fonts();
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  update_imgui_overlay_size();
  ImGui::NewFrame();

  draw_aimbot_fov_imgui();
  draw_thirdperson_crosshair_imgui();
  draw_players_imgui();
  draw_backtrack_visualizer_imgui();
  draw_projectile_debug_imgui();
  hitmarker::draw_imgui();
  navbot::controller().draw_imgui();

  draw_watermark();
  draw_game_indicators();

  if (menu_focused) {
    draw_menu();
  }

  ImGui::Render();
}

static bool record_overlay_commands(uint32_t image_index)
{
  if (image_index >= swapchain_image_count) {
    return false;
  }

  auto& frame = frames[image_index];
  if (frame.CommandBuffer == VK_NULL_HANDLE || frame.Framebuffer == VK_NULL_HANDLE || frame.Fence == VK_NULL_HANDLE) {
    return false;
  }

  const auto fence_timeout = cathook::core::is_detach_pending()
    ? 0
    : static_cast<uint64_t>(overlay_fence_wait_timeout.count()) * 1000ULL * 1000ULL;
  auto result = vkWaitForFences(vk_device, 1, &frame.Fence, VK_TRUE, fence_timeout);
  if (result == VK_TIMEOUT) {
    if (!logged_overlay_fence_timeout) {
      print("Vulkan overlay fence wait timed out; disabling Vulkan overlay for this injection\n");
      logged_overlay_fence_timeout = true;
    }
    vulkan_overlay_disabled.store(true, std::memory_order_release);
    return false;
  }
  if (result != VK_SUCCESS) {
    print("vkWaitForFences failed: %d\n", result);
    return false;
  }

  result = vkResetFences(vk_device, 1, &frame.Fence);
  if (result != VK_SUCCESS) {
    print("vkResetFences failed: %d\n", result);
    return false;
  }

  result = vkResetCommandBuffer(frame.CommandBuffer, 0);
  if (result != VK_SUCCESS) {
    print("vkResetCommandBuffer failed: %d\n", result);
    return false;
  }

  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  result = vkBeginCommandBuffer(frame.CommandBuffer, &begin_info);
  if (result != VK_SUCCESS) {
    print("vkBeginCommandBuffer failed: %d\n", result);
    return false;
  }

  VkImageMemoryBarrier begin_barrier = {};
  begin_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  begin_barrier.srcAccessMask = 0;
  begin_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  begin_barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  begin_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  begin_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  begin_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  begin_barrier.image = frame.Backbuffer;
  begin_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  begin_barrier.subresourceRange.baseMipLevel = 0;
  begin_barrier.subresourceRange.levelCount = 1;
  begin_barrier.subresourceRange.baseArrayLayer = 0;
  begin_barrier.subresourceRange.layerCount = 1;

  vkCmdPipelineBarrier(
    frame.CommandBuffer,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &begin_barrier);

  VkRenderPassBeginInfo render_pass_info = {};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_info.renderPass = vk_render_pass;
  render_pass_info.framebuffer = frame.Framebuffer;
  render_pass_info.renderArea.extent = get_overlay_extent();

  vkCmdBeginRenderPass(frame.CommandBuffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
  draw_imgui_overlay();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.CommandBuffer);
  vkCmdEndRenderPass(frame.CommandBuffer);

  VkImageMemoryBarrier end_barrier = {};
  end_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  end_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  end_barrier.dstAccessMask = 0;
  end_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  end_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  end_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  end_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  end_barrier.image = frame.Backbuffer;
  end_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  end_barrier.subresourceRange.baseMipLevel = 0;
  end_barrier.subresourceRange.levelCount = 1;
  end_barrier.subresourceRange.baseArrayLayer = 0;
  end_barrier.subresourceRange.layerCount = 1;

  vkCmdPipelineBarrier(
    frame.CommandBuffer,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &end_barrier);

  result = vkEndCommandBuffer(frame.CommandBuffer);
  if (result != VK_SUCCESS) {
    print("vkEndCommandBuffer failed: %d\n", result);
    return false;
  }

  return true;
}

static bool submit_overlay_commands(VkQueue queue, const VkPresentInfoKHR* present_info, uint32_t image_index)
{
  if (present_info->waitSemaphoreCount > max_present_wait_semaphores) {
    print("Too many Vulkan present wait semaphores: %u\n", present_info->waitSemaphoreCount);
    return false;
  }

  auto& frame = frames[image_index];
  auto& semaphores = frame_semaphores[image_index];

  std::array<VkPipelineStageFlags, max_present_wait_semaphores> wait_stages = {};
  std::fill(wait_stages.begin(), wait_stages.end(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

  VkSubmitInfo submit_info = {};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.waitSemaphoreCount = present_info->waitSemaphoreCount;
  submit_info.pWaitSemaphores = present_info->pWaitSemaphores;
  submit_info.pWaitDstStageMask = wait_stages.data();
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &frame.CommandBuffer;
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &semaphores.RenderCompleteSemaphore;

  const auto result = vkQueueSubmit(queue, 1, &submit_info, frame.Fence);
  if (result != VK_SUCCESS) {
    print("vkQueueSubmit overlay failed: %d\n", result);
    return false;
  }

  if (!logged_first_overlay_submit) {
    const auto* draw_data = ImGui::GetDrawData();
    print("Vulkan overlay submitted: image=%u wait_semaphores=%u cmd_lists=%d vertices=%d\n",
      image_index,
      present_info->waitSemaphoreCount,
      draw_data != nullptr ? draw_data->CmdListsCount : 0,
      draw_data != nullptr ? draw_data->TotalVtxCount : 0);
    logged_first_overlay_submit = true;
  }

  return true;
}

VkResult queue_present_hook(VkQueue queue, const VkPresentInfoKHR* present_info)
{
  if (queue_present_original == nullptr) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  if (vulkan_overlay_disabled.load(std::memory_order_acquire) ||
      cathook::core::is_detach_pending() ||
      nographics::should_skip_rendering_hooks() ||
      present_info == nullptr ||
      present_info->swapchainCount != 1 ||
      present_info->pSwapchains == nullptr ||
      present_info->pImageIndices == nullptr) {
    const auto result = queue_present_original(queue, present_info);
    cathook::core::service_detach_request();
    return result;
  }

  const auto swapchain = present_info->pSwapchains[0];
  const auto image_index = present_info->pImageIndices[0];
  const auto render_queue = select_render_queue(queue);

  if (vk_device == VK_NULL_HANDLE || sdl_window == nullptr || !ensure_imgui_context() ||
      !ensure_swapchain_resources(swapchain, render_queue) || !ensure_vulkan_renderer(render_queue) ||
      !record_overlay_commands(image_index) || !submit_overlay_commands(render_queue.queue, present_info, image_index)) {
    const auto result = queue_present_original(queue, present_info);
    cathook::core::service_detach_request();
    return result;
  }

  auto overlay_present_info = *present_info;
  auto overlay_complete = frame_semaphores[image_index].RenderCompleteSemaphore;
  overlay_present_info.waitSemaphoreCount = 1;
  overlay_present_info.pWaitSemaphores = &overlay_complete;

  const auto result = queue_present_original(queue, &overlay_present_info);
  cathook::core::service_detach_request();
  return result;
}

VkResult create_swapchain_hook(VkDevice device, const VkSwapchainCreateInfoKHR* create_info, const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain)
{
  if (create_swapchain_original == nullptr) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  vk_device = device;
  vk_allocator = allocator;
  destroy_swapchain_resources(true);

  const auto result = create_swapchain_original(device, create_info, allocator, swapchain);
  if (result != VK_SUCCESS || create_info == nullptr || swapchain == nullptr || *swapchain == VK_NULL_HANDLE) {
    return result;
  }

  vk_image_extent = create_info->imageExtent;
  active_swapchain_format = create_info->imageFormat;
  min_image_count = std::max(create_info->minImageCount, 2u);
  active_swapchain = VK_NULL_HANDLE;
  return result;
}

void destroy_swapchain_hook(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* allocator)
{
  if (swapchain == active_swapchain || device == vk_device) {
    vk_device = device;
    vk_allocator = allocator;
    destroy_swapchain_resources(true);
  }

  if (destroy_swapchain_original != nullptr) {
    destroy_swapchain_original(device, swapchain, allocator);
  }
}

VkResult acquire_next_image_hook(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* image_index)
{
  vk_device = device;
  return acquire_next_image_original(device, swapchain, timeout, semaphore, fence, image_index);
}

VkResult acquire_next_image2_hook(VkDevice device, const VkAcquireNextImageInfoKHR* acquire_info, uint32_t* image_index)
{
  vk_device = device;
  return acquire_next_image2_original(device, acquire_info, image_index);
}

VkResult create_device_hook(VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info, const VkAllocationCallbacks* allocator, VkDevice* device)
{
  const auto result = create_device_original(physical_device, create_info, allocator, device);
  if (result == VK_SUCCESS && device != nullptr && *device != VK_NULL_HANDLE) {
    vk_physical_device = physical_device;
    vk_device = *device;
    vk_allocator = allocator;
  }

  return result;
}

void get_device_queue_hook(VkDevice device, uint32_t family, uint32_t queue_index, VkQueue* queue)
{
  get_device_queue_original(device, family, queue_index, queue);
  if (queue != nullptr) {
    remember_queue_family(*queue, family);
  }
}

void get_device_queue2_hook(VkDevice device, const VkDeviceQueueInfo2* queue_info, VkQueue* queue)
{
  get_device_queue2_original(device, queue_info, queue);
  if (queue_info != nullptr && queue != nullptr) {
    remember_queue_family(*queue, queue_info->queueFamilyIndex);
  }
}
