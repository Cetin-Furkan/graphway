#include "init/wayland_init.h"
#include <stdio.h>
#include <string.h>
#include <linux/input-event-codes.h>

#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#endif

static uint32_t calculate_hover_state(struct Application *app, double x, double y) {
    double w = (double)app->wl.width;

    double dx_close = x - (w - 20.0);
    double dy_close = y - 16.0;
    if ((dx_close * dx_close + dy_close * dy_close) <= (9.0 * 9.0)) {
        return 1;
    }

    double dx_full = x - (w - 44.0);
    double dy_full = y - 16.0;
    if ((dx_full * dx_full + dy_full * dy_full) <= (9.0 * 9.0)) {
        return 2;
    }

    return 0;
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)serial; (void)surface;
    struct Application *app = data;
    app->wl.pointer_inside = true;
    app->wl.pointer_x = wl_fixed_to_double(sx);
    app->wl.pointer_y = wl_fixed_to_double(sy);

    uint32_t new_hover = calculate_hover_state(app, app->wl.pointer_x, app->wl.pointer_y);
    if (new_hover != app->wl.hover_state) {
        app->wl.hover_state = new_hover;
        app->wl.is_dirty = true;
    }
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface) {
    (void)pointer; (void)serial; (void)surface;
    struct Application *app = data;
    app->wl.pointer_inside = false;
    app->wl.is_dragging = false;

    if (app->wl.hover_state != 0) {
        app->wl.hover_state = 0;
        app->wl.is_dirty = true;
    }
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)time;
    struct Application *app = data;
    app->wl.pointer_x = wl_fixed_to_double(sx);
    app->wl.pointer_y = wl_fixed_to_double(sy);

    if (app->wl.is_dragging) {
        double dx = (app->wl.pointer_x - app->wl.drag_start_x);
        double dy = (app->wl.pointer_y - app->wl.drag_start_y);

        if (app->wl.render_mode == 1) {
            // 3D MODE: Left-drag rotates 3D Orbit Camera Angles
            app->wl.rot_y = app->wl.rot_start_y + (float)dx * 0.008f;
            app->wl.rot_x = app->wl.rot_start_x + (float)dy * 0.008f;
        } else {
            // 2D MODE: Left-drag pans 2D canvas coordinates
            dx /= (double)app->wl.height;
            dy /= (double)app->wl.height;
            app->wl.pan_x = app->wl.pan_start_x - (float)dx * (2.0f / app->wl.zoom_level);
            app->wl.pan_y = app->wl.pan_start_y - (float)dy * (2.0f / app->wl.zoom_level);
        }
        app->wl.is_dirty = true;
    }

    uint32_t new_hover = calculate_hover_state(app, app->wl.pointer_x, app->wl.pointer_y);
    if (new_hover != app->wl.hover_state) {
        app->wl.hover_state = new_hover;
        app->wl.is_dirty = true;
    }
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer; (void)time;
    struct Application *app = data;

    if (button == BTN_LEFT) {
        if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
            double x = app->wl.pointer_x;
            double y = app->wl.pointer_y;
            double w = (double)app->wl.width;
            double h = (double)app->wl.height;

            // 1. Close Button Click
            double dx_close = x - (w - 20.0);
            double dy_close = y - 16.0;
            if ((dx_close * dx_close + dy_close * dy_close) <= (9.0 * 9.0)) {
                app->wl.running = false;
                return;
            }

            // 2. Fullscreen Button Click
            double dx_full = x - (w - 44.0);
            double dy_full = y - 16.0;
            if ((dx_full * dx_full + dy_full * dy_full) <= (9.0 * 9.0)) {
                if (!app->wl.is_fullscreen) {
                    xdg_toplevel_set_fullscreen(app->wl.xdg_toplevel, NULL);
                } else {
                    xdg_toplevel_unset_fullscreen(app->wl.xdg_toplevel);
                }
                return;
            }

            // 3. Border Edge Resizing
            const double margin = 4.0;
            uint32_t edge = 0;

            if (y < margin) edge |= XDG_TOPLEVEL_RESIZE_EDGE_TOP;
            if (y > h - margin) edge |= XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
            if (x < margin) edge |= XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
            if (x > w - margin) edge |= XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;

            if (edge != 0) {
                xdg_toplevel_resize(app->wl.xdg_toplevel, app->wl.seat, serial, edge);
                return;
            }

            // 4. Headerbar Dragging
            if (y <= 32.0) {
                xdg_toplevel_move(app->wl.xdg_toplevel, app->wl.seat, serial);
                return;
            }

            // 5. Canvas Drag Click
            app->wl.is_dragging = true;
            app->wl.drag_start_x = x;
            app->wl.drag_start_y = y;
            app->wl.pan_start_x = app->wl.pan_x;
            app->wl.pan_start_y = app->wl.pan_y;
            app->wl.rot_start_x = app->wl.rot_x;
            app->wl.rot_start_y = app->wl.rot_y;
        } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
            app->wl.is_dragging = false;
        }
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    (void)pointer; (void)time;
    struct Application *app = data;

    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        double scroll_val = wl_fixed_to_double(value);
        float zoom_factor = (scroll_val > 0.0) ? 0.9f : 1.1111f;

        app->wl.zoom_level *= zoom_factor;

        if (app->wl.zoom_level < 0.1f) app->wl.zoom_level = 0.1f;
        if (app->wl.zoom_level > 50.0f) app->wl.zoom_level = 50.0f;

        app->wl.is_dirty = true;
    }
}

static void pointer_frame(void *data, struct wl_pointer *pointer) { (void)data; (void)pointer; }
static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source) { (void)data; (void)pointer; (void)axis_source; }
static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis) { (void)data; (void)pointer; (void)time; (void)axis; }
static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) { (void)data; (void)pointer; (void)axis; (void)discrete; }

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    struct Application *app = data;

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && app->wl.pointer == NULL) {
        app->wl.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->wl.pointer, &pointer_listener, app);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && app->wl.pointer != NULL) {
        wl_pointer_destroy(app->wl.pointer);
        app->wl.pointer = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) { (void)data; (void)seat; (void)name; }

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct Application *app = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    app->wl.configured = true;

    if (app->wl.surface != nullptr) {
        wl_surface_commit(app->wl.surface);
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                                    int32_t width, int32_t height, struct wl_array *states) {
    (void)xdg_toplevel;
    struct Application *app = data;

    if (width > 0 && height > 0) {
        app->wl.width = (uint32_t)width;
        app->wl.height = (uint32_t)height;
    }

    app->wl.is_fullscreen = false;
    uint32_t *state;
    wl_array_for_each(state, states) {
        if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN) {
            app->wl.is_fullscreen = true;
        }
    }
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    (void)xdg_toplevel;
    struct Application *app = data;
    app->wl.running = false;
}

static void xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height) { (void)data; (void)xdg_toplevel; (void)width; (void)height; }
static void xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities) { (void)data; (void)xdg_toplevel; (void)capabilities; }

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
    .configure_bounds = xdg_toplevel_configure_bounds,
    .wm_capabilities = xdg_toplevel_wm_capabilities,
};

static void registry_handler(void *data, struct wl_registry *registry, uint32_t id,
                             const char *interface, uint32_t version) {
    (void)version;
    struct Application *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->wl.compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wl.xdg_wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(app->wl.xdg_wm_base, &xdg_wm_base_listener, app);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        app->wl.seat = wl_registry_bind(registry, id, &wl_seat_interface, 5);
        wl_seat_add_listener(app->wl.seat, &seat_listener, app);
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handler,
    .global_remove = nullptr,
};

bool wayland_init(struct Application *app) {
    app->wl.running = true;
    app->wl.configured = false;
    app->wl.width = DEFAULT_WIDTH;
    app->wl.height = DEFAULT_HEIGHT;
    app->wl.pointer = NULL;
    app->wl.seat = NULL;
    app->wl.is_fullscreen = false;
    app->wl.hover_state = 0;
    app->wl.is_dirty = false;

    // 3D Orbit Mode enabled by default
    app->wl.render_mode = 1;
    app->wl.zoom_level = 1.0f;
    app->wl.pan_x = 0.0f;
    app->wl.pan_y = 0.0f;
    app->wl.rot_x = 0.4f;
    app->wl.rot_y = 0.6f;
    app->wl.is_dragging = false;

    app->wl.display = wl_display_connect(nullptr);
    if (app->wl.display == nullptr) {
        fprintf(stderr, "Error: Failed to connect to Wayland display server.\n");
        return false;
    }

    app->wl.registry = wl_display_get_registry(app->wl.display);
    wl_registry_add_listener(app->wl.registry, &registry_listener, app);
    wl_display_roundtrip(app->wl.display);

    if (app->wl.compositor == nullptr || app->wl.xdg_wm_base == nullptr) {
        fprintf(stderr, "Error: Compositor missing required xdg-shell interfaces.\n");
        return false;
    }

    app->wl.surface = wl_compositor_create_surface(app->wl.compositor);
    app->wl.xdg_surface = xdg_wm_base_get_xdg_surface(app->wl.xdg_wm_base, app->wl.surface);
    xdg_surface_add_listener(app->wl.xdg_surface, &xdg_surface_listener, app);

    app->wl.xdg_toplevel = xdg_surface_get_toplevel(app->wl.xdg_surface);
    xdg_toplevel_add_listener(app->wl.xdg_toplevel, &xdg_toplevel_listener, app);
    xdg_toplevel_set_title(app->wl.xdg_toplevel, "WayPlot Engine");

    wl_surface_commit(app->wl.surface);
    wl_display_roundtrip(app->wl.display);

    printf("Wayland connection, surface, and input initialized.\n");
    return true;
}