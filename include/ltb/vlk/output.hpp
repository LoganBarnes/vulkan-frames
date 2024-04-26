// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/vlk/setup.hpp"

// standard
#include <vector>

namespace ltb::vlk
{

template < AppType app_type >
struct OutputData;

template <>
struct OutputData< AppType::Headless >
{
    VkExtent2D     framebuffer_size   = { };
    VkImage        color_image        = { };
    VkDeviceMemory color_image_memory = { };
    VkImageView    color_image_view   = { };
    VkFramebuffer  framebuffer        = { };
};

template <>
struct OutputData< AppType::Windowed >
{
    // Output
    VkExtent2D                   framebuffer_size      = { };
    VkSwapchainKHR               swapchain             = { };
    std::vector< VkImage >       swapchain_images      = { };
    std::vector< VkImageView >   swapchain_image_views = { };
    std::vector< VkFramebuffer > framebuffers          = { };
};

auto initialize(
    VkExtent3D                            image_extents,
    ExternalMemory                        external_memory,
    SetupData< AppType::Headless > const& setup,
    OutputData< AppType::Headless >&      output
) -> bool;

auto initialize(
    SetupData< AppType::Windowed > const& setup,
    OutputData< AppType::Windowed >&      output
) -> bool;

template < AppType app_type >
auto destroy( SetupData< app_type > const& setup, OutputData< app_type >& output ) -> void;

} // namespace ltb::vlk
