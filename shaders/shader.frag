#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "content_plot.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 u_resolution;
    uint u_hover_state;
    uint u_render_mode;    // 0 = 2D Mode, 1 = 3D Mode
    vec2 u_pan;
    float u_zoom;
    float u_rot_x;          // 3D Pitch
    float u_rot_y;          // 3D Yaw
    uint u_point_count;
    uint64_t u_plot_data;
} push;

vec3 srgbToLinear(vec3 color) {
    return pow(color, vec3(2.2));
}

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

float sdCircle(vec2 p, float r) {
    return length(p) - r;
}

float sdBoxOutline(vec2 p, vec2 b, float thickness) {
    return abs(sdRoundedBox(p, b, 0.5)) - thickness * 0.5;
}

void main() {
    vec2 pixel_pos = (uv * 0.5 + vec2(0.5)) * push.u_resolution;
    vec2 center = push.u_resolution * 0.5;
    vec2 p = pixel_pos - center;
    vec2 half_size = push.u_resolution * 0.5;

    float corner_radius = 4.0;
    float border_thickness = 1.0;

    // 1. 4px Rounded Corner Window Mask
    float d_window = sdRoundedBox(p, half_size, corner_radius);
    float window_alpha = clamp(0.5 - d_window, 0.0, 1.0);

    if (window_alpha <= 0.0) {
        discard;
    }

    // 2. Render Decoupled Plot Content with 2D/3D Mode Switch
    float aspect = push.u_resolution.x / push.u_resolution.y;
    vec3 bg_color = render_plot_content(uv, aspect, push.u_resolution, push.u_pan, push.u_zoom,
                                       push.u_rot_x, push.u_rot_y, push.u_render_mode,
                                       push.u_plot_data, push.u_point_count);

    // CSD UI Colors
    vec3 red_normal   = srgbToLinear(vec3(0.90, 0.15, 0.18));
    vec3 red_hover    = srgbToLinear(vec3(1.00, 0.32, 0.35));

    vec3 green_normal = srgbToLinear(vec3(0.18, 0.68, 0.32));
    vec3 green_hover  = srgbToLinear(vec3(0.30, 0.82, 0.45));

    vec3 red_btn   = (push.u_hover_state == 1) ? red_hover : red_normal;
    vec3 green_btn = (push.u_hover_state == 2) ? green_hover : green_normal;

    vec3 border_color = srgbToLinear(vec3(0.35, 0.38, 0.45));
    vec3 header_color = srgbToLinear(vec3(0.14, 0.16, 0.20));
    vec3 white_icon   = srgbToLinear(vec3(1.00, 1.00, 1.00));

    // 3. Render 1px Outer Border
    float d_border = abs(d_window + border_thickness * 0.5) - (border_thickness * 0.5);
    float border_alpha = clamp(0.5 - d_border, 0.0, 1.0);
    vec3 final_color = mix(bg_color, border_color, border_alpha * 0.8);

    // 4. Render Headerbar (Top 32px)
    float is_headerbar = step(pixel_pos.y, 32.0);
    final_color = mix(final_color, header_color, is_headerbar);

    // 5. Render Close Button
    vec2 close_center = vec2(push.u_resolution.x - 20.0, 16.0);
    vec2 close_p = pixel_pos - close_center;

    float d_close_btn = sdCircle(close_p, 9.0);
    float close_btn_alpha = clamp(0.5 - d_close_btn, 0.0, 1.0);
    final_color = mix(final_color, red_btn, close_btn_alpha);

    float stroke = 1.2;
    float line1 = sdSegment(close_p, vec2(-3.5, -3.5), vec2(3.5, 3.5)) - stroke * 0.5;
    float line2 = sdSegment(close_p, vec2(-3.5, 3.5), vec2(3.5, -3.5)) - stroke * 0.5;
    float d_x = min(line1, line2);
    float x_alpha = clamp(0.5 - d_x, 0.0, 1.0);
    final_color = mix(final_color, white_icon, x_alpha);

    // 6. Render Fullscreen Button
    vec2 full_center = vec2(push.u_resolution.x - 44.0, 16.0);
    vec2 full_p = pixel_pos - full_center;

    float d_full_btn = sdCircle(full_p, 9.0);
    float full_btn_alpha = clamp(0.5 - d_full_btn, 0.0, 1.0);
    final_color = mix(final_color, green_btn, full_btn_alpha);

    float d_square_icon = sdBoxOutline(full_p, vec2(3.0, 3.0), 1.2);
    float sq_alpha = clamp(0.5 - d_square_icon, 0.0, 1.0);
    final_color = mix(final_color, white_icon, sq_alpha);

    // Pre-Multiplied Alpha Output
    outColor = vec4(final_color * window_alpha, window_alpha);
}
