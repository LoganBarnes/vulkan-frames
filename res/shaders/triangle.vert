#version 450

// Constants
const float PI = 3.14159265359F;

// Uniforms
layout (push_constant) uniform Model
{
    vec4 scale_rotation_translation;
} model;

// Logic
void main()
{
    const float scale      = model.scale_rotation_translation.x;
    const float rotation   = model.scale_rotation_translation.y;
    const vec2  translation = model.scale_rotation_translation.zw;

    const float angle = rotation - PI * 2.0F / 3.0F * float(gl_VertexIndex);

    const vec2 local_position = vec2(cos(angle), sin(angle)) * scale + translation;

    gl_Position = vec4(local_position, 0.5F, 1.0F);
}
