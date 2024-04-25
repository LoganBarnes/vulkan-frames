#version 450

// Uniforms
layout (push_constant, std430) uniform Display {
    layout (offset = 16) vec4 color; // offset must be the size of previous push_constants
} display;

// Outputs
layout (location = 0) out vec4 out_color;

// Logic
void main() {
    out_color = display.color;
}
