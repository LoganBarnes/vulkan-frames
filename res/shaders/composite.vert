#version 450

// Output
layout(location = 0) out vec2 texture_coordinates;

// Logic
void main()
{
    switch (gl_VertexIndex)
    {
        // Bottom left
        case 0:
        gl_Position         = vec4(-1.0F, +1.0F, 0.0F, 1.0F);
        texture_coordinates = vec2(0.0F, 1.0F);
        break;

        // Bottom right
        case 1:
        gl_Position         = vec4(+1.0F, +1.0F, 0.0F, 1.0F);
        texture_coordinates = vec2(1.0F, 1.0F);
        break;

        // Top left
        case 2:
        gl_Position         = vec4(-1.0F, -1.0F, 0.0F, 1.0F);
        texture_coordinates = vec2(0.0F, 0.0F);
        break;

        // Top right
        case 3:
        gl_Position         = vec4(+1.0F, -1.0F, 0.0F, 1.0F);
        texture_coordinates = vec2(1.0F, 0.0F);
        break;

        default :
        gl_Position         = vec4(vec3(0.0F), 1.0F);
        texture_coordinates = vec2(0.0F);
        break;
    }
}
