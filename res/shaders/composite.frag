#version 450

// Uniforms
layout(binding = 0) uniform sampler2D tex_sampler1;
//layout(binding = 2) uniform sampler2D tex_sampler2;
//layout(binding = 3) uniform sampler2D tex_sampler3;
//layout(binding = 4) uniform sampler2D tex_sampler4;

// Inputs
layout(location = 0) in vec2 texture_coordinates;

// Outputs
layout(location = 0) out vec4 out_color;

// Logic
void update_color(in sampler2D tex)
{
    vec4 color = texture(tex, texture_coordinates).rgba;
    out_color.rgb = mix(out_color.rgb, color.rgb, color.a);
}

void main()
{
    out_color = vec4(texture_coordinates, 1.0F, 1.0F);
    //    out_color = vec4(0.0F, 0.0F, 0.0F, 1.0F);

    update_color(tex_sampler1);
    //    update_color(tex_sampler2);
    //    update_color(tex_sampler3);
    //    update_color(tex_sampler4);
}
