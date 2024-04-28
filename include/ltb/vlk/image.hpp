// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/vlk/setup.hpp"

namespace ltb::vlk
{

template < ExternalMemory mem_type >
struct ImageData
{
    VkExtent2D           image_size          = { };
    VkImage              color_image         = { };
    VkMemoryRequirements memory_requirements = { };
    uint32               memory_type_index   = { };
    VkDeviceMemory       color_image_memory  = { };
    VkImageView          color_image_view    = { };
};

/// \brief Initialize all the fields of an ImageData struct.
template < ExternalMemory mem_type >
auto initialize(
    ImageData< mem_type >&  image,
    VkPhysicalDevice const& physical_device,
    VkDevice const&         device,
    VkExtent3D              image_extents,
    VkFormat                color_format,
    int32                   import_image_fd
) -> bool;

/// \brief A wrapper function around the main initialize function.
template < ExternalMemory mem_type, AppType setup_app_type >
auto initialize(
    ImageData< mem_type >&             image,
    SetupData< setup_app_type > const& setup,
    VkExtent3D                         image_extents,
    int32                              import_image_fd
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
        image,
        setup.physical_device,
        setup.device,
        image_extents,
        color_format,
        import_image_fd
    );
}

/// \brief Get the file descriptor of an image with external memory storage.
auto get_file_descriptor(
    int32&                file_descriptor,
    VkInstance const&     instance,
    VkDevice const&       device,
    VkDeviceMemory const& image_memory
) -> bool;

/// \brief Get the file descriptor of an image with external memory storage.
template < AppType setup_app_type >
auto get_file_descriptor(
    int32&                                     file_descriptor,
    SetupData< setup_app_type > const&         setup,
    ImageData< ExternalMemory::Export > const& image
) -> bool
{
    return get_file_descriptor(
        file_descriptor,
        setup.instance,
        setup.device,
        image.color_image_memory
    );
}

/// \brief Destroy all the fields of an ImageData struct.
template < ExternalMemory mem_type >
auto destroy( ImageData< mem_type >& image, VkDevice const& device ) -> void;

/// \brief A wrapper function around the main destroy function.
template < ExternalMemory mem_type, AppType setup_app_type >
auto destroy( ImageData< mem_type >& image, SetupData< setup_app_type > const& setup ) -> void
{
    return destroy( image, setup.device );
}

} // namespace ltb::vlk
