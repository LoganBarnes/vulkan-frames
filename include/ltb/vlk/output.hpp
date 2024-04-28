// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/vlk/check.hpp"
#include "ltb/vlk/texture.hpp"

namespace ltb::vlk
{

template < AppType app_type >
struct OutputData;

template <>
struct OutputData< AppType::Windowed >
{
    VkRenderPass                 render_pass           = { };
    VkSwapchainKHR               swapchain             = { };
    std::vector< VkImage >       swapchain_images      = { };
    std::vector< VkImageView >   swapchain_image_views = { };
    std::vector< VkFramebuffer > framebuffers          = { };
    VkExtent2D                   framebuffer_size      = { };
};

template <>
struct OutputData< AppType::Headless >
{
    VkRenderPass  render_pass      = { };
    VkFramebuffer framebuffer      = { };
    VkExtent2D    framebuffer_size = { };
};

/// \brief Initialize all the fields of a windowed OutputData struct.
auto initialize(
    OutputData< AppType::Windowed >& output,
    GLFWwindow*                      window,
    VkSurfaceKHR const&              surface,
    VkPhysicalDevice const&          physical_device,
    VkDevice const&                  device,
    uint32                           graphics_queue_family_index,
    uint32                           surface_queue_family_index,
    VkSurfaceFormatKHR const&        surface_format
) -> bool;

/// \brief Initialize all the fields of a headless OutputData struct.
auto initialize(
    OutputData< AppType::Headless >& output,
    VkDevice const&                  device,
    VkImageView const&               color_image_view,
    VkExtent3D const&                image_extent,
    VkFormat                         color_format
) -> bool;

/// \brief A wrapper function around the main initialize function.
auto initialize(
    OutputData< AppType::Windowed >&      output,
    SetupData< AppType::Windowed > const& setup
) -> bool;

/// \brief A wrapper function around the main initialize function.
template < AppType setup_app_type, ExternalMemory mem_type >
auto initialize(
    OutputData< AppType::Headless >&   output,
    SetupData< setup_app_type > const& setup,
    Image< mem_type > const&           image
) -> bool
{
    auto color_format = VkFormat{ };
    if constexpr ( setup_app_type == AppType::Windowed )
    {
        color_format = setup.surface_format.format;
    }
    else
    {
        color_format = setup.color_format;
    }
    return initialize(
        output,
        setup.device,
        image.color_image_view,
        image.image_size,
        color_format
    );
}

/// \brief Destroy all the fields of an OutputData struct.
template < AppType app_type >
auto destroy( OutputData< app_type >& output, VkDevice const& device ) -> void;

/// \brief A wrapper function around the main destroy function.
template < AppType output_app_type, AppType setup_app_type >
auto destroy( OutputData< output_app_type >& output, SetupData< setup_app_type > const& setup )
    -> void
{
    return destroy( output, setup.device );
}

} // namespace ltb::vlk
