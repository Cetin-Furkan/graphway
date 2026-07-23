#include "init/vulkan_init.h"
#include "init/wayland_init.h"
#include "graph_core/vulkan_core.h"

static const char *APP_NAME    = "WayPlot Engine";
static const char *ENGINE_NAME = "WayPlot Graphics Core";

FeatureCheckStatus check_gpu_features(VkPhysicalDevice gpu, const struct EngineRequestedFeatures *requested) {
    VkPhysicalDeviceVulkan14Features supported14 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pNext = nullptr
    };

    VkPhysicalDeviceVulkan13Features supported13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &supported14
    };

    VkPhysicalDeviceVulkan12Features supported12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &supported13
    };

    VkPhysicalDeviceFeatures2 supported2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &supported12
    };

    vkGetPhysicalDeviceFeatures2(gpu, &supported2);

    uint32_t total_requested = 0;
    uint32_t total_rejected = 0;

    #define AUDIT_FLAG(req_val, supp_val, name) \
        if ((req_val) == VK_TRUE) { \
            total_requested++; \
            if ((supp_val) != VK_TRUE) { \
                fprintf(stderr, "  [REJECTED] Feature '%s' requested but NOT supported by GPU.\n", name); \
                total_rejected++; \
            } \
        }

    AUDIT_FLAG(requested->base.samplerAnisotropy, supported2.features.samplerAnisotropy, "Sampler Anisotropy");
    AUDIT_FLAG(requested->base.fillModeNonSolid, supported2.features.fillModeNonSolid, "Wireframe Fill Mode");
    AUDIT_FLAG(requested->v12.timelineSemaphore, supported12.timelineSemaphore, "Timeline Semaphores");
    AUDIT_FLAG(requested->v12.bufferDeviceAddress, supported12.bufferDeviceAddress, "Buffer Device Address");
    AUDIT_FLAG(requested->v12.descriptorIndexing, supported12.descriptorIndexing, "Descriptor Indexing");
    AUDIT_FLAG(requested->v13.dynamicRendering, supported13.dynamicRendering, "Dynamic Rendering");
    AUDIT_FLAG(requested->v13.synchronization2, supported13.synchronization2, "Synchronization2");
    AUDIT_FLAG(requested->v14.pushDescriptor, supported14.pushDescriptor, "Push Descriptors");
    AUDIT_FLAG(requested->v14.hostImageCopy, supported14.hostImageCopy, "Host Image Copy");
    AUDIT_FLAG(requested->v14.maintenance5, supported14.maintenance5, "Maintenance 5");
    AUDIT_FLAG(requested->v14.maintenance6, supported14.maintenance6, "Maintenance 6");

    #undef AUDIT_FLAG

    if (total_rejected == 0) {
        return FEATURE_CHECK_ACCEPT_ALL;
    } else if (total_rejected == total_requested) {
        return FEATURE_CHECK_REJECT_ALL;
    } else {
        return FEATURE_CHECK_REJECT_SOME;
    }
}

bool create_instance(VkInstance *out_instance) {
    const char *extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
    };

    const VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = APP_NAME,
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .pEngineName = ENGINE_NAME,
        .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .apiVersion = TARGET_VK_VERSION,
    };

    const VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = extensions,
    };

    if (vkCreateInstance(&create_info, nullptr, out_instance) != VK_SUCCESS) {
        fprintf(stderr, "Error: vkCreateInstance failed.\n");
        return false;
    }

    printf("Vulkan 1.4 Instance created.\n");
    return true;
}

bool create_wayland_surface(struct Application *app) {
    const VkWaylandSurfaceCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .display = app->wl.display,
        .surface = app->wl.surface,
    };

    if (vkCreateWaylandSurfaceKHR(app->vk.instance, &create_info, nullptr, &app->vk.surface) != VK_SUCCESS) {
        fprintf(stderr, "Error: Failed to create VkWaylandSurfaceKHR.\n");
        return false;
    }

    printf("Vulkan surface created.\n");
    return true;
}

bool select_gpu_and_verify(struct Application *app, const struct EngineRequestedFeatures *req_features) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(app->vk.instance, &count, nullptr);

    if (count == 0) {
        fprintf(stderr, "Error: No physical Vulkan GPUs found.\n");
        return false;
    }

    VkPhysicalDevice gpus[count];
    vkEnumeratePhysicalDevices(app->vk.instance, &count, gpus);

    for (uint32_t i = 0; i < count; ++i) {
        if (check_gpu_features(gpus[i], req_features) != FEATURE_CHECK_ACCEPT_ALL) {
            continue;
        }

        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &family_count, nullptr);

        VkQueueFamilyProperties families[family_count];
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &family_count, families);

        for (uint32_t q = 0; q < family_count; ++q) {
            if (families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                VkBool32 present_support = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(gpus[i], q, app->vk.surface, &present_support);

                if (present_support == VK_TRUE) {
                    app->vk.physical_device = gpus[i];
                    app->vk.graphics_queue_family = q;

                    VkPhysicalDeviceProperties props;
                    vkGetPhysicalDeviceProperties(gpus[i], &props);

                    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                        app->vk.is_integrated_gpu = true;
                        printf("Selected GPU: %s [INTEGRATED GPU: UMA Zero-Copy Mode Enabled]\n", props.deviceName);
                    } else {
                        app->vk.is_integrated_gpu = false;
                        printf("Selected GPU: %s [DISCRETE GPU: VRAM Staging Mode Enabled]\n", props.deviceName);
                    }

                    return true;
                }
            }
        }
    }

    fprintf(stderr, "Error: No GPU met feature and presentation criteria.\n");
    return false;
}

bool create_device(struct Application *app, const struct EngineRequestedFeatures *req_features) {
    const float queue_priority = DEFAULT_QUEUE_PRIO;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = app->vk.graphics_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    const char *device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkPhysicalDeviceVulkan14Features enable_v14 = req_features->v14;
    enable_v14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    enable_v14.pNext = nullptr;

    VkPhysicalDeviceVulkan13Features enable_v13 = req_features->v13;
    enable_v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enable_v13.pNext = &enable_v14;

    VkPhysicalDeviceVulkan12Features enable_v12 = req_features->v12;
    enable_v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enable_v12.pNext = &enable_v13;

    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enable_v12,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures = &req_features->base,
    };

    if (vkCreateDevice(app->vk.physical_device, &device_info, nullptr, &app->vk.device) != VK_SUCCESS) {
        fprintf(stderr, "Error: vkCreateDevice failed.\n");
        return false;
    }

    vkGetDeviceQueue(app->vk.device, app->vk.graphics_queue_family, 0, &app->vk.graphics_queue);
    printf("Logical VkDevice created.\n");
    return true;
}

bool application_init(struct Application *app, const struct EngineRequestedFeatures *req_features) {
    if (!wayland_init(app)) return false;
    if (!create_instance(&app->vk.instance)) return false;
    if (!create_wayland_surface(app)) return false;
    if (!select_gpu_and_verify(app, req_features)) return false;
    if (!create_device(app, req_features)) return false;
    if (!create_swapchain(app)) return false;
    if (!create_commands_and_sync(app)) return false;
    if (!create_graphics_pipeline(app)) return false;

    return true;
}