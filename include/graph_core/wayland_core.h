#ifndef WAYLAND_CORE_H
#define WAYLAND_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include "wayland/xdg-shell-client-protocol.h"

struct WaylandContext {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *xdg_wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    // Input state (wl_seat)
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    double pointer_x;
    double pointer_y;
    bool pointer_inside;

    // Mode Switch & Camera State
    uint32_t render_mode; // 0 = 2D Mode, 1 = 3D Mode
    float zoom_level;
    float pan_x;
    float pan_y;
    float rot_x;          // 3D Pitch Angle
    float rot_y;          // 3D Yaw Angle

    bool is_dragging;     // Unified Drag Handler (2D Pan & 3D Rotate)
    double drag_start_x;
    double drag_start_y;
    float pan_start_x;
    float pan_start_y;
    float rot_start_x;
    float rot_start_y;

    // Hover & Event-Driven Redraw state
    uint32_t hover_state;
    bool is_dirty;

    uint32_t width;
    uint32_t height;
    bool configured;
    bool running;
    bool is_fullscreen;
};

#endif // WAYLAND_CORE_H
