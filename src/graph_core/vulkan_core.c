#include "graph_core/vulkan_core.h"

static uint32_t find_memory_type(VkPhysicalDevice gpu, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

static bool allocate_raw_buffer(struct Application *app, VkDeviceSize size, VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags properties, struct VulkanBuffer *out_buffer) {
    out_buffer->size = size;
    out_buffer->mapped_ptr = nullptr;

    const VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = size,
        .usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(app->vk.device, &buffer_info, nullptr, &out_buffer->handle) != VK_SUCCESS) {
        fprintf(stderr, "Error: vkCreateBuffer failed.\n");
        return false;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(app->vk.device, out_buffer->handle, &mem_reqs);

    uint32_t mem_type_index = find_memory_type(app->vk.physical_device, mem_reqs.memoryTypeBits, properties);
    if (mem_type_index == UINT32_MAX) {
        // Fallback for iGPUs if HOST_VISIBLE + DEVICE_LOCAL isn't reported together
        mem_type_index = find_memory_type(app->vk.physical_device, mem_reqs.memoryTypeBits,
                                                  properties & ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    const VkMemoryAllocateFlagsInfo flags_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .pNext = nullptr,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, // Required for BDA 64-bit pointers
        .deviceMask = 0,
    };

    const VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &flags_info,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = mem_type_index,
    };

    if (vkAllocateMemory(app->vk.device, &alloc_info, nullptr, &out_buffer->memory) != VK_SUCCESS) {
        fprintf(stderr, "Error: vkAllocateMemory failed.\n");
        return false;
    }

    vkBindBufferMemory(app->vk.device, out_buffer->handle, out_buffer->memory, 0);

    // Get 64-bit GPU Buffer Device Address
    const VkBufferDeviceAddressInfo address_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .pNext = nullptr,
        .buffer = out_buffer->handle,
    };
    out_buffer->device_address = vkGetBufferDeviceAddress(app->vk.device, &address_info);

    return true;
}

void destroy_buffer(VkDevice device, struct VulkanBuffer *buffer) {
    if (buffer->mapped_ptr != nullptr) {
        vkUnmapMemory(device, buffer->memory);
        buffer->mapped_ptr = nullptr;
    }
    if (buffer->handle != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer->handle, nullptr);
        buffer->handle = VK_NULL_HANDLE;
    }
    if (buffer->memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffer->memory, nullptr);
        buffer->memory = VK_NULL_HANDLE;
    }
    buffer->device_address = 0;
}

bool upload_plot_data(struct Application *app, const Point2D *points, uint32_t count) {
    if (count == 0 || points == nullptr) return false;

    VkDeviceSize data_size = sizeof(Point2D) * count;

    // Clean up existing buffer if resized or present
    if (app->vk.plot_buffer.handle != VK_NULL_HANDLE) {
        destroy_buffer(app->vk.device, &app->vk.plot_buffer);
    }

    if (app->vk.is_integrated_gpu) {
        // ====================================================================
        // INTEGRATED GPU PATH: Zero-Copy Unified Memory Architecture
        // ====================================================================
        VkMemoryPropertyFlags iGPU_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (!allocate_raw_buffer(app, data_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, iGPU_flags, &app->vk.plot_buffer)) {
            return false;
        }

        vkMapMemory(app->vk.device, app->vk.plot_buffer.memory, 0, data_size, 0, &app->vk.plot_buffer.mapped_ptr);
        memcpy(app->vk.plot_buffer.mapped_ptr, points, (size_t)data_size);

        printf("[iGPU OPTIMIZATION] Zero-Copy direct upload: %u points (%.2f KB) -> Shared Memory.\n",
               count, (double)data_size / 1024.0);
    } else {
        // ====================================================================
        // DISCRETE GPU PATH: PCIe Staging Buffer -> High-Speed VRAM
        // ====================================================================
        struct VulkanBuffer staging;
        VkMemoryPropertyFlags staging_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        if (!allocate_raw_buffer(app, data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_flags, &staging)) {
            return false;
        }

        vkMapMemory(app->vk.device, staging.memory, 0, data_size, 0, &staging.mapped_ptr);
        memcpy(staging.mapped_ptr, points, (size_t)data_size);
        vkUnmapMemory(app->vk.device, staging.memory);

        // Allocate Device-Local VRAM Buffer
        VkMemoryPropertyFlags vram_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        VkBufferUsageFlags vram_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        if (!allocate_raw_buffer(app, data_size, vram_usage, vram_flags, &app->vk.plot_buffer)) {
            destroy_buffer(app->vk.device, &staging);
            return false;
        }

        // Record PCIe copy command
        vkResetCommandBuffer(app->vk.command_buffer, 0);
        const VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(app->vk.command_buffer, &begin_info);

        const VkBufferCopy copy_region = { .srcOffset = 0, .dstOffset = 0, .size = data_size };
        vkCmdCopyBuffer(app->vk.command_buffer, staging.handle, app->vk.plot_buffer.handle, 1, &copy_region);
        vkEndCommandBuffer(app->vk.command_buffer);

        const VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &app->vk.command_buffer,
        };
        vkQueueSubmit(app->vk.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(app->vk.graphics_queue);

        destroy_buffer(app->vk.device, &staging);

        printf("[dGPU OPTIMIZATION] PCIe Staging upload: %u points (%.2f KB) -> VRAM.\n",
               count, (double)data_size / 1024.0);
    }

    return true;
}

bool create_swapchain(struct Application *app) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->vk.physical_device, app->vk.surface, &caps);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->vk.physical_device, app->vk.surface, &format_count, nullptr);
    VkSurfaceFormatKHR formats[format_count];
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->vk.physical_device, app->vk.surface, &format_count, formats);

    VkSurfaceFormatKHR chosen_format = formats[0];
    for (uint32_t i = 0; i < format_count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen_format = formats[i];
            break;
        }
    }

    VkExtent2D extent = {
        .width = app->wl.width,
        .height = app->wl.height,
    };

    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
        composite_alpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }

    const VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = app->vk.surface,
        .minImageCount = image_count,
        .imageFormat = chosen_format.format,
        .imageColorSpace = chosen_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = caps.currentTransform,
        .compositeAlpha = composite_alpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    if (vkCreateSwapchainKHR(app->vk.device, &create_info, nullptr, &app->vk.swapchain.handle) != VK_SUCCESS) {
        fprintf(stderr, "Error: vkCreateSwapchainKHR failed.\n");
        return false;
    }

    app->vk.swapchain.image_format = chosen_format.format;
    app->vk.swapchain.color_space = chosen_format.colorSpace;
    app->vk.swapchain.extent = extent;

    vkGetSwapchainImagesKHR(app->vk.device, app->vk.swapchain.handle, &app->vk.swapchain.image_count, nullptr);
    app->vk.swapchain.images = malloc(sizeof(VkImage) * app->vk.swapchain.image_count);
    vkGetSwapchainImagesKHR(app->vk.device, app->vk.swapchain.handle, &app->vk.swapchain.image_count, app->vk.swapchain.images);

    app->vk.swapchain.image_views = malloc(sizeof(VkImageView) * app->vk.swapchain.image_count);

    for (uint32_t i = 0; i < app->vk.swapchain.image_count; ++i) {
        const VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = app->vk.swapchain.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = app->vk.swapchain.image_format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        if (vkCreateImageView(app->vk.device, &view_info, nullptr, &app->vk.swapchain.image_views[i]) != VK_SUCCESS) {
            fprintf(stderr, "Error: vkCreateImageView failed for index %u.\n", i);
            return false;
        }
    }

    printf("Swapchain created successfully (%u images, %ux%u resolution).\n",
           app->vk.swapchain.image_count, extent.width, extent.height);
    return true;
}

void destroy_swapchain(struct Application *app) {
    if (app->vk.swapchain.image_views != nullptr) {
        for (uint32_t i = 0; i < app->vk.swapchain.image_count; ++i) {
            vkDestroyImageView(app->vk.device, app->vk.swapchain.image_views[i], nullptr);
        }
        free(app->vk.swapchain.image_views);
        app->vk.swapchain.image_views = nullptr;
    }

    if (app->vk.swapchain.images != nullptr) {
        free(app->vk.swapchain.images);
        app->vk.swapchain.images = nullptr;
    }

    if (app->vk.swapchain.handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(app->vk.device, app->vk.swapchain.handle, nullptr);
        app->vk.swapchain.handle = VK_NULL_HANDLE;
    }
}

bool recreate_swapchain(struct Application *app) {
    if (app->wl.width == 0 || app->wl.height == 0) {
        return true;
    }

    vkDeviceWaitIdle(app->vk.device);

    destroy_swapchain(app);

    if (!create_swapchain(app)) {
        fprintf(stderr, "Error: Failed to recreate Vulkan swapchain.\n");
        return false;
    }

    return true;
}

bool load_shader_module(VkDevice device, const char *filepath, VkShaderModule *out_module) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open shader file '%s'. Did you run 'glslc'?\n", filepath);
        return false;
    }

    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (filesize <= 0 || (filesize % 4) != 0) {
        fprintf(stderr, "Error: Invalid SPIR-V file size for '%s'.\n", filepath);
        fclose(file);
        return false;
    }

    uint32_t *buffer = malloc(filesize);
    if (fread(buffer, 1, filesize, file) != (size_t)filesize) {
        fprintf(stderr, "Error: Failed to read shader file '%s'.\n", filepath);
        free(buffer);
        fclose(file);
        return false;
    }
    fclose(file);

    const VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = filesize,
        .pCode = buffer,
    };

    VkResult res = vkCreateShaderModule(device, &create_info, nullptr, out_module);
    free(buffer);

    if (res != VK_SUCCESS) {
        fprintf(stderr, "Error: vkCreateShaderModule failed for '%s' (%d).\n", filepath, res);
        return false;
    }
    return true;
}

bool create_graphics_pipeline(struct Application *app) {
    VkShaderModule vert_module = VK_NULL_HANDLE;
    VkShaderModule frag_module = VK_NULL_HANDLE;

    if (!load_shader_module(app->vk.device, "shaders/vert.spv", &vert_module) ||
        !load_shader_module(app->vk.device, "shaders/frag.spv", &frag_module)) {
        return false;
    }

    const VkPipelineShaderStageCreateInfo shader_stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_module,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_module,
            .pName = "main",
        }
    };

    const VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0,
    };

    const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    const VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    const VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    const VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    const VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    };

    const VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };

    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    const VkPipelineDynamicStateCreateInfo dynamic_state_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };

    const VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(struct PushConstants),
    };

    const VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .setLayoutCount = 0,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };

    if (vkCreatePipelineLayout(app->vk.device, &pipeline_layout_info, nullptr, &app->vk.pipeline_layout) != VK_SUCCESS) {
        fprintf(stderr, "Error: vkCreatePipelineLayout failed.\n");
        vkDestroyShaderModule(app->vk.device, vert_module, nullptr);
        vkDestroyShaderModule(app->vk.device, frag_module, nullptr);
        return false;
    }

    const VkPipelineRenderingCreateInfo rendering_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &app->vk.swapchain.image_format,
    };

    const VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_create_info,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state_info,
        .layout = app->vk.pipeline_layout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
    };

    if (vkCreateGraphicsPipelines(app->vk.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &app->vk.graphics_pipeline) != VK_SUCCESS) {
        fprintf(stderr, "Error: vkCreateGraphicsPipelines failed.\n");
        vkDestroyPipelineLayout(app->vk.device, app->vk.pipeline_layout, nullptr);
        vkDestroyShaderModule(app->vk.device, vert_module, nullptr);
        vkDestroyShaderModule(app->vk.device, frag_module, nullptr);
        return false;
    }

    vkDestroyShaderModule(app->vk.device, vert_module, nullptr);
    vkDestroyShaderModule(app->vk.device, frag_module, nullptr);

    printf("Graphics pipeline created successfully.\n");
    return true;
}

bool create_commands_and_sync(struct Application *app) {
    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = app->vk.graphics_queue_family,
    };

    if (vkCreateCommandPool(app->vk.device, &pool_info, nullptr, &app->vk.command_pool) != VK_SUCCESS) {
        fprintf(stderr, "Error: vkCreateCommandPool failed.\n");
        return false;
    }

    const VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = app->vk.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    if (vkAllocateCommandBuffers(app->vk.device, &alloc_info, &app->vk.command_buffer) != VK_SUCCESS) {
        fprintf(stderr, "Error: vkAllocateCommandBuffers failed.\n");
        return false;
    }

    const VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    if (vkCreateSemaphore(app->vk.device, &semaphore_info, nullptr, &app->vk.image_available_semaphore) != VK_SUCCESS ||
        vkCreateSemaphore(app->vk.device, &semaphore_info, nullptr, &app->vk.render_finished_semaphore) != VK_SUCCESS ||
        vkCreateFence(app->vk.device, &fence_info, nullptr, &app->vk.in_flight_fence) != VK_SUCCESS) {
        fprintf(stderr, "Error: Failed to create sync objects.\n");
        return false;
    }

    return true;
}

void draw_frame(struct Application *app) {
    vkWaitForFences(app->vk.device, 1, &app->vk.in_flight_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(app->vk.device, 1, &app->vk.in_flight_fence);

    uint32_t image_index = 0;
    VkResult acquire_res = vkAcquireNextImageKHR(app->vk.device, app->vk.swapchain.handle, UINT64_MAX,
                                                 app->vk.image_available_semaphore, VK_NULL_HANDLE, &image_index);

    if (acquire_res != VK_SUCCESS && acquire_res != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "Error: vkAcquireNextImageKHR failed (%d).\n", acquire_res);
        return;
    }

    vkResetCommandBuffer(app->vk.command_buffer, 0);

    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pInheritanceInfo = nullptr,
    };

    vkBeginCommandBuffer(app->vk.command_buffer, &begin_info);

    const VkImageMemoryBarrier2 barrier_to_attach = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = app->vk.swapchain.images[image_index],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    const VkDependencyInfo dep_info_attach = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier_to_attach,
    };

    vkCmdPipelineBarrier2(app->vk.command_buffer, &dep_info_attach);

    const VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 0.0f}}};

    const VkRenderingAttachmentInfo color_attachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = app->vk.swapchain.image_views[image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clear_color,
    };

    const VkRenderingInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderArea = {
            .offset = {0, 0},
            .extent = app->vk.swapchain.extent,
        },
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
        .pDepthAttachment = nullptr,
        .pStencilAttachment = nullptr,
    };

    vkCmdBeginRendering(app->vk.command_buffer, &rendering_info);

    vkCmdBindPipeline(app->vk.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, app->vk.graphics_pipeline);

    const VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)app->vk.swapchain.extent.width,
        .height = (float)app->vk.swapchain.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(app->vk.command_buffer, 0, 1, &viewport);

    const VkRect2D scissor = {
        .offset = {0, 0},
        .extent = app->vk.swapchain.extent,
    };
    vkCmdSetScissor(app->vk.command_buffer, 0, 1, &scissor);

    const struct PushConstants push_data = {
        .window_width    = (float)app->vk.swapchain.extent.width,
        .window_height   = (float)app->vk.swapchain.extent.height,
        .hover_state     = app->wl.hover_state,
        .render_mode     = app->wl.render_mode, // Pass Mode Switch (0 = 2D, 1 = 3D)
        .pan_x           = app->wl.pan_x,
        .pan_y           = app->wl.pan_y,
        .zoom_level      = app->wl.zoom_level,
        .rot_x           = app->wl.rot_x,        // Pass 3D Pitch Angle
        .rot_y           = app->wl.rot_y,        // Pass 3D Yaw Angle
        .point_count     = (app->vk.plot_buffer.handle != VK_NULL_HANDLE) ? 200 : 0,
        .plot_data_addr  = app->vk.plot_buffer.device_address,
    };
    
    vkCmdPushConstants(
        app->vk.command_buffer,
        app->vk.pipeline_layout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(struct PushConstants),
        &push_data
    );

    vkCmdDraw(app->vk.command_buffer, 3, 1, 0, 0);

    vkCmdEndRendering(app->vk.command_buffer);

    const VkImageMemoryBarrier2 barrier_to_present = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = app->vk.swapchain.images[image_index],
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    const VkDependencyInfo dep_info_present = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier_to_present,
    };

    vkCmdPipelineBarrier2(app->vk.command_buffer, &dep_info_present);
    vkEndCommandBuffer(app->vk.command_buffer);

    const VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &app->vk.image_available_semaphore,
        .pWaitDstStageMask = &wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &app->vk.command_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &app->vk.render_finished_semaphore,
    };

    vkQueueSubmit(app->vk.graphics_queue, 1, &submit_info, app->vk.in_flight_fence);

    const VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &app->vk.render_finished_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &app->vk.swapchain.handle,
        .pImageIndices = &image_index,
        .pResults = nullptr,
    };

    VkResult present_res = vkQueuePresentKHR(app->vk.graphics_queue, &present_info);
    if (present_res != VK_SUCCESS && present_res != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "Error: vkQueuePresentKHR failed (%d).\n", present_res);
    }

    wl_display_flush(app->wl.display);
}

void application_cleanup(struct Application *app) {
    if (app->vk.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(app->vk.device);

        destroy_buffer(app->vk.device, &app->vk.plot_buffer);

        if (app->vk.graphics_pipeline) vkDestroyPipeline(app->vk.device, app->vk.graphics_pipeline, nullptr);
        if (app->vk.pipeline_layout) vkDestroyPipelineLayout(app->vk.device, app->vk.pipeline_layout, nullptr);

        if (app->vk.image_available_semaphore) vkDestroySemaphore(app->vk.device, app->vk.image_available_semaphore, nullptr);
        if (app->vk.render_finished_semaphore) vkDestroySemaphore(app->vk.device, app->vk.render_finished_semaphore, nullptr);
        if (app->vk.in_flight_fence) vkDestroyFence(app->vk.device, app->vk.in_flight_fence, nullptr);
        if (app->vk.command_pool) vkDestroyCommandPool(app->vk.device, app->vk.command_pool, nullptr);
    }

    destroy_swapchain(app);

    if (app->vk.device != nullptr) {
        vkDestroyDevice(app->vk.device, nullptr);
    }
    if (app->vk.surface != nullptr) {
        vkDestroySurfaceKHR(app->vk.instance, app->vk.surface, nullptr);
    }
    if (app->vk.instance != nullptr) {
        vkDestroyInstance(app->vk.instance, nullptr);
    }
    if (app->wl.xdg_toplevel != nullptr) {
        xdg_toplevel_destroy(app->wl.xdg_toplevel);
    }
    if (app->wl.xdg_surface != nullptr) {
        xdg_surface_destroy(app->wl.xdg_surface);
    }
    if (app->wl.surface != nullptr) {
        wl_surface_destroy(app->wl.surface);
    }
    if (app->wl.xdg_wm_base != nullptr) {
        xdg_wm_base_destroy(app->wl.xdg_wm_base);
    }
    if (app->wl.compositor != nullptr) {
        wl_compositor_destroy(app->wl.compositor);
    }
    if (app->wl.registry != nullptr) {
        wl_registry_destroy(app->wl.registry);
    }
    if (app->wl.display != nullptr) {
        wl_display_disconnect(app->wl.display);
    }
    printf("Application cleanly destroyed.\n");
}