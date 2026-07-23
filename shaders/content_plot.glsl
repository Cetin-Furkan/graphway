// ============================================================================
// CONTENT PLOT MODULE (Unified 2D Plot / 3D Raymarched Object Engine)
// ============================================================================

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(buffer_reference, scalar) readonly buffer PlotPointBuffer {
    vec2 points[];
};

// 2D Line Segment helper
float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// 2D Grid Helper
float draw_grid2D(vec2 p, float spacing, float line_width_px) {
    vec2 g = abs(fract(p / spacing - 0.5) - 0.5) * spacing;
    vec2 d = g / (fwidth(p) + 1e-5);
    float line = min(d.x, d.y);
    return clamp(1.0 - line / line_width_px, 0.0, 1.0);
}

// 2D Infinite Grid Renderer
vec3 render_2d_grid(vec2 world_pos) {
    vec3 bg_color = pow(vec3(0.06, 0.07, 0.10), vec3(2.2));
    
    float world_scale = length(fwidth(world_pos));
    float target_spacing = world_scale * 40.0;
    
    float log_step = pow(10.0, floor(log2(target_spacing) / log2(10.0)));
    float minor_spacing = log_step;
    float major_spacing = log_step * 10.0;

    float minor_grid = draw_grid2D(world_pos, minor_spacing, 1.0);
    float major_grid = draw_grid2D(world_pos, major_spacing, 1.5);

    vec3 minor_color = pow(vec3(0.15, 0.18, 0.24), vec3(2.2));
    vec3 major_color = pow(vec3(0.25, 0.30, 0.40), vec3(2.2));

    vec3 color = mix(bg_color, minor_color, minor_grid * 0.6);
    color = mix(color, major_color, major_grid * 0.8);

    vec2 axis_dist = abs(world_pos) / (fwidth(world_pos) + 1e-5);
    float axis_x_line = clamp(1.5 - axis_dist.y, 0.0, 1.0);
    float axis_y_line = clamp(1.5 - axis_dist.x, 0.0, 1.0);

    vec3 axis_x_color = pow(vec3(0.85, 0.25, 0.25), vec3(2.2));
    vec3 axis_y_color = pow(vec3(0.25, 0.75, 0.35), vec3(2.2));

    color = mix(color, axis_x_color, axis_x_line * 0.9);
    color = mix(color, axis_y_color, axis_y_line * 0.9);

    return color;
}

// ============================================================================
// 3D RAYMARCHING ENGINE
// ============================================================================

float sdBox3D(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// 3D Object Geometry
float map3D(vec3 p, mat3 rot) {
    p = rot * p; // Apply mouse orbit rotation matrix

    float arm = 0.55;
    float thick = 0.16;
    float radius = 0.03;

    float d1 = sdBox3D(p, vec3(arm, thick, thick)) - radius;
    float d2 = sdBox3D(p, vec3(thick, arm, thick)) - radius;
    float d3 = sdBox3D(p, vec3(thick, thick, arm)) - radius;

    return min(d1, min(d2, d3));
}

vec3 calcNormal3D(vec3 p, mat3 rot) {
    vec2 e = vec2(0.001, 0.0);
    return normalize(vec3(
        map3D(p + e.xyy, rot) - map3D(p - e.xyy, rot),
        map3D(p + e.yxy, rot) - map3D(p - e.yxy, rot),
        map3D(p + e.yyx, rot) - map3D(p - e.yyx, rot)
    ));
}

float calcAO3D(vec3 pos, vec3 nor, mat3 rot) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 0; i < 5; i++) {
        float h = 0.01 + 0.08 * float(i);
        float d = map3D(pos + h * nor, rot);
        occ += (h - d) * sca;
        sca *= 0.95;
    }
    return clamp(1.0 - 2.0 * occ, 0.0, 1.0);
}

// Single Pass Render evaluating either 2D or 3D Mode
vec3 raymarch_pass(vec2 uv_coords, float aspect, vec2 pan, float zoom, float rot_x, float rot_y, uint render_mode, uint64_t bda_addr, uint point_count) {
    if (render_mode == 0) {
        // ====================================================================
        // MODE 0: 2D PLOT MODE
        // ====================================================================
        vec2 world_pos = (uv_coords + pan) / zoom;
        world_pos.x *= aspect;

        vec3 col = render_2d_grid(world_pos);

        if (bda_addr != 0UL && point_count > 1) {
            PlotPointBuffer dataset = PlotPointBuffer(bda_addr);

            float min_dist = 1e5;
            for (uint i = 0; i < point_count - 1; i++) {
                vec2 pA = dataset.points[i];
                vec2 pB = dataset.points[i + 1];

                float dist = sdSegment(world_pos, pA, pB);
                min_dist = min(min_dist, dist);
            }

            float pixel_size = length(fwidth(world_pos));
            float line_width = pixel_size * 2.5;
            float line_alpha = clamp((line_width - min_dist) / pixel_size, 0.0, 1.0);

            vec3 curve_color = pow(vec3(0.0, 0.8, 1.0), vec3(2.2));
            col = mix(col, curve_color, line_alpha);
        }

        return col;
    } else {
        // ====================================================================
        // MODE 1: 3D OBJECT / GRAPH MODE
        // ====================================================================
        vec2 st = uv_coords;
        st.x *= aspect;

        // Camera setup with Zoom
        vec3 ro = vec3(pan.x, pan.y, 2.5 / zoom);
        vec3 rd = normalize(vec3(st, -1.5));

        // Background Gradient
        vec3 col = mix(pow(vec3(0.04, 0.05, 0.09), vec3(2.2)), pow(vec3(0.12, 0.15, 0.22), vec3(2.2)), (uv_coords.y + 1.0) * 0.5);

        // Compute 3D Rotation Matrices from mouse drag angles
        float cx = cos(rot_x), sx = sin(rot_x);
        mat3 rx = mat3(1, 0, 0,  0, cx, -sx,  0, sx, cx);

        float cy = cos(rot_y), sy = sin(rot_y);
        mat3 ry = mat3(cy, 0, sy,  0, 1, 0,  -sy, 0, cy);

        mat3 rot = rx * ry;

        // Raymarch 3D Object
        float t = 0.0;
        for (int i = 0; i < 128; i++) {
            vec3 p = ro + rd * t;
            float d = map3D(p, rot);
            if (d < 0.0005) {
                vec3 n = calcNormal3D(p, rot);
                vec3 lightDir = normalize(vec3(0.8, 1.2, 1.0));

                float diff = max(dot(n, lightDir), 0.0);
                vec3 ref = reflect(-lightDir, n);
                float spec = pow(max(dot(ref, -rd), 0.0), 32.0);
                float rim = pow(1.0 - max(dot(-rd, n), 0.0), 4.0);
                float ao = calcAO3D(p, n, rot);

                vec3 matColor = pow(vec3(0.92, 0.94, 0.98), vec3(2.2));
                vec3 ambient  = pow(vec3(0.12, 0.15, 0.22), vec3(2.2)) * ao;

                col = matColor * (diff * pow(vec3(1.0, 0.96, 0.88), vec3(2.2)) + ambient) + 
                      spec * vec3(1.0) + 
                      rim * pow(vec3(0.3, 0.5, 0.9), vec3(2.2)) * 0.4;
                break;
            }
            if (t > 10.0) break;
            t += d;
        }

        return col;
    }
}

// 4x SSAA Anti-Aliasing Wrapper
vec3 render_plot_content(vec2 uv_coords, float aspect, vec2 resolution, vec2 pan, float zoom, float rot_x, float rot_y, uint render_mode, uint64_t bda_addr, uint point_count) {
    vec2 offsets[4] = vec2[](
        vec2(-0.25, -0.25) / resolution,
        vec2( 0.25, -0.25) / resolution,
        vec2(-0.25,  0.25) / resolution,
        vec2( 0.25,  0.25) / resolution
    );

    vec3 color_sum = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        color_sum += raymarch_pass(uv_coords + offsets[i] * 2.0, aspect, pan, zoom, rot_x, rot_y, render_mode, bda_addr, point_count);
    }

    return color_sum * 0.25;
}
