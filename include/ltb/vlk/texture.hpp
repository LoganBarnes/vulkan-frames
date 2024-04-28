// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/vlk/setup.hpp"

namespace ltb::vlk
{

template < ExternalMemory mem_type >
struct Image
{
    VkExtent2D     image_size         = { };
    VkImage        color_image        = { };
    VkDeviceMemory color_image_memory = { };
    VkImageView    color_image_view   = { };
};

template < ExternalMemory mem_type >
auto initialize(
    VkPhysicalDevice const& physical_device,
    VkDevice const&         device,
    VkExtent3D              image_extents,
    VkFormat                color_format,
    Image< mem_type >&      image
) -> bool;

template < ExternalMemory mem_type >
auto destroy( VkDevice const& device, Image< mem_type >& image ) -> void;

} // namespace ltb::vlk
