#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "init/vulkan_init.h"
#include "init/wayland_init.h"
#include "graph_core/vulkan_core.h"
#include "graph_core/wayland_core.h"

int main(void) {
    struct Application app = {0};

    const struct EngineRequestedFeatures requested_features = {
        .base = {
            .samplerAnisotropy = VK_TRUE,
            .fillModeNonSolid  = VK_TRUE,
        },
        .v12 = {
            .timelineSemaphore   = VK_TRUE,
            .bufferDeviceAddress = VK_TRUE,
            .descriptorIndexing  = VK_TRUE,
        },
        .v13 = {
            .dynamicRendering    = VK_TRUE,
            .synchronization2     = VK_TRUE,
        },
        .v14 = {
            .pushDescriptor      = VK_TRUE,
            .hostImageCopy       = VK_TRUE,
            .maintenance5        = VK_TRUE,
            .maintenance6        = VK_TRUE,
        }
    };

    if (!application_init(&app, &requested_features)) {
        fprintf(stderr, "Error: Initialization failed.\n");
        application_cleanup(&app);
        return EXIT_FAILURE;
    }

    // Set Render Mode: 1 = 3D Mode, 0 = 2D Plot Mode
    app.wl.render_mode = 0;

    // Generate 200 Math Plot Points
    const uint32_t point_count = 200;
    Point2D points[point_count];

    for (uint32_t i = 0; i < point_count; ++i) {
        float x = -3.0f + ((float)i / (float)(point_count - 1)) * 6.0f;
        float y = sinf(5.0f * x) * expf(-0.3f * x * x);
        points[i] = (Point2D){ .x = x, .y = y };
    }

    if (!upload_plot_data(&app, points, point_count)) {
        fprintf(stderr, "Error: Failed to upload plot dataset to GPU.\n");
        application_cleanup(&app);
        return EXIT_FAILURE;
    }

    application_run(&app);
    application_cleanup(&app);
    return EXIT_SUCCESS;
}