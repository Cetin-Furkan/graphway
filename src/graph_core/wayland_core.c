#include "graph_core/wayland_core.h"
#include "graph_core/vulkan_core.h"

void application_run(struct Application *app) {
    printf("Rendering initial frame with 4x SSAA anti-aliased content...\n");

    draw_frame(app);

    printf("Event loop running on-demand (0.0%% idle CPU/GPU)...\n");

    while (app->wl.running) {
        // Wait on Wayland socket until an input or desktop event arrives
        if (wl_display_dispatch(app->wl.display) == -1) {
            fprintf(stderr, "Wayland display connection lost.\n");
            break;
        }

        // Handle window resize / snapping
        if (app->wl.width != app->vk.swapchain.extent.width ||
            app->wl.height != app->vk.swapchain.extent.height) {
            
            if (recreate_swapchain(app)) {
                app->wl.is_dirty = true;
            }
        }

        // Redraw only if dirty (e.g., hover changed or window resized)
        if (app->wl.is_dirty) {
            draw_frame(app);
            app->wl.is_dirty = false; // Reset dirty flag immediately
        }
    }
}