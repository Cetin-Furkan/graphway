#ifndef VULKAN_CORE_H
#define VULKAN_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>

#include "graph_core/wayland_core.h"

#define TARGET_VK_VERSION VK_API_VERSION_1_4
#define DEFAULT_QUEUE_PRIO 1.0f

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600

typedef struct {
    float x;
    float y;
} Point2D;

struct VulkanBuffer {
    VkBuffer handle;
    VkDeviceMemory memory;
    VkDeviceAddress device_address;
    void *mapped_ptr;
    VkDeviceSize size;
};

// Perfectly aligned 48-byte Push Constants struct
struct PushConstants {
    float window_width;      // Offset  0
    float window_height;     // Offset  4
    uint32_t hover_state;    // Offset  8
    uint32_t render_mode;    // Offset 12 (0 = 2D Plot, 1 = 3D Object)
    float pan_x;             // Offset 16
    float pan_y;             // Offset 20
    float zoom_level;        // Offset 24
    float rot_x;             // Offset 28 (3D Pitch Angle)
    float rot_y;             // Offset 32 (3D Yaw Angle)
    uint32_t point_count;    // Offset 36
    uint64_t plot_data_addr; // Offset 40 (64-bit BDA Pointer aligned to 8 bytes)
};

typedef enum {
    FEATURE_CHECK_ACCEPT_ALL = 0,
    FEATURE_CHECK_REJECT_SOME,
    FEATURE_CHECK_REJECT_ALL
} FeatureCheckStatus;

struct EngineRequestedFeatures {
    VkPhysicalDeviceFeatures base;
    VkPhysicalDeviceVulkan12Features v12;
    VkPhysicalDeviceVulkan13Features v13;
    VkPhysicalDeviceVulkan14Features v14;
};

struct VulkanSwapchain {
    VkSwapchainKHR handle;
    VkFormat image_format;
    VkColorSpaceKHR color_space;
    VkExtent2D extent;
    uint32_t image_count;
    VkImage *images;
    VkImageView *image_views;
};

struct VulkanContext {
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    uint32_t graphics_queue_family;
    struct VulkanSwapchain swapchain;

    bool is_integrated_gpu;
    struct VulkanBuffer plot_buffer;

    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;

    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore image_available_semaphore;
    VkSemaphore render_finished_semaphore;
    VkFence in_flight_fence;
};

struct Application {
    struct WaylandContext wl;
    struct VulkanContext vk;
};

bool create_swapchain(struct Application *app);
void destroy_swapchain(struct Application *app);
bool recreate_swapchain(struct Application *app);

bool load_shader_module(VkDevice device, const char *filepath, VkShaderModule *out_module);
bool create_graphics_pipeline(struct Application *app);

bool create_commands_and_sync(struct Application *app);
void draw_frame(struct Application *app);

bool upload_plot_data(struct Application *app, const Point2D *points, uint32_t count);
void destroy_buffer(VkDevice device, struct VulkanBuffer *buffer);

void application_run(struct Application *app);
void application_cleanup(struct Application *app);

#endif // VULKAN_CORE_H
