# Vulkan Frames

An attempt to share Vulkan frames between multiple processes.

## Applications

| Application                | Description                                                            | Status      |
|----------------------------|------------------------------------------------------------------------|-------------|
| `basic_triangle_app`       | A simple Vulkan application that renders a triangle.                   | Complete    |
| `framebuffer_triangle_app` | Renders a triangle to a texture then displays the texture.             | Complete    |
| `external_triangle_app`    | Same as `framebuffer_triangle_app` but with different logical devices. | Development |
| `frames_app`               | Renders a triangle image and sends it to a different app.              | Broken      |
| `composite_app`            | Displays a texture sent from a different app.                          | Broken      |
