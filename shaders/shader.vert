#version 450

layout(location = 0) out vec2 uv;

void main() {
    // 3 vertices generating a full-screen triangle in NDC space (-1 to 1)
    vec2 pos[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    uv = pos[gl_VertexIndex];
    gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
}
