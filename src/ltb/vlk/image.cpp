// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#include "ltb/vlk/image.hpp"

// project
#include "ltb/vlk/check.hpp"

// standard
#include <optional>

// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_external_memory.html

namespace ltb::vlk
{
namespace
{

auto constexpr external_memory_handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

auto get_memory_type_index(
    VkPhysicalDevice const&     physical_device,
    VkMemoryRequirements const& memory_requirements
)
{
    auto memory_props = VkPhysicalDeviceMemoryProperties{ };
    ::vkGetPhysicalDeviceMemoryProperties( physical_device, &memory_props );
    auto const mem_prop_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    auto memory_type_index = std::optional< uint32 >{ };
    for ( auto i = 0u; i < memory_props.memoryTypeCount; ++i )
    {
        auto const type_is_suitable = ( 0 != ( memory_requirements.memoryTypeBits & ( 1u << i ) ) );
        auto const props_exist
            = ( memory_props.memoryTypes[ i ].propertyFlags & mem_prop_flags ) == mem_prop_flags;

        if ( ( !memory_type_index ) && type_is_suitable && props_exist )
        {
            memory_type_index = uint32{ i };
        }
    }

    return memory_type_index;
}

} // namespace

template < ExternalMemory mem_type >
auto initialize(
    ImageData< mem_type >&  image,
    VkPhysicalDevice const& physical_device,
    VkDevice const&         device,
    VkExtent3D const        image_extents,
    VkFormat const          color_format,
    int32 const             import_image_fd
) -> bool
{
    image.image_size = VkExtent2D{ image_extents.width, image_extents.height };

    auto color_image_create_info = VkImageCreateInfo{
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .imageType             = VK_IMAGE_TYPE_2D,
        .format                = color_format,
        .extent                = image_extents,
        .mipLevels             = 1U,
        .arrayLayers           = 1U,
        .samples               = VK_SAMPLE_COUNT_1_BIT,
        .tiling                = VK_IMAGE_TILING_OPTIMAL,
        .usage                 = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0U,
        .pQueueFamilyIndices   = nullptr,
        .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if constexpr ( mem_type == ExternalMemory::None )
    {
        CHECK_VK( ::vkCreateImage( device, &color_image_create_info, nullptr, &image.color_image )
        );
        spdlog::debug( "vkCreateImage()" );
    }
    else
    {
        auto const external_color_image_info = VkExternalMemoryImageCreateInfo{
            .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .pNext       = nullptr,
            .handleTypes = external_memory_handle_type,
        };
        color_image_create_info.pNext = &external_color_image_info;

        CHECK_VK( ::vkCreateImage( device, &color_image_create_info, nullptr, &image.color_image )
        );
        spdlog::debug( "vkCreateImage() w/ external memory" );
    }

    ::vkGetImageMemoryRequirements( device, image.color_image, &image.memory_requirements );

    if ( auto const memory_type_index
         = get_memory_type_index( physical_device, image.memory_requirements ) )
    {
        image.memory_type_index = memory_type_index.value( );
    }
    else
    {
        spdlog::error( "No suitable memory type found" );
        return false;
    }

    auto color_image_alloc_info = VkMemoryAllocateInfo{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = nullptr,
        .allocationSize  = image.memory_requirements.size,
        .memoryTypeIndex = image.memory_type_index,
    };

    if constexpr ( mem_type == ExternalMemory::Export )
    {
        auto const export_image_memory_info = VkExportMemoryAllocateInfo{
            .sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
            .pNext       = nullptr,
            .handleTypes = external_memory_handle_type,
        };
        color_image_alloc_info.pNext = &export_image_memory_info;

        CHECK_VK( ::vkAllocateMemory(
            device,
            &color_image_alloc_info,
            nullptr,
            &image.color_image_memory
        ) );
        spdlog::debug( "vkAllocateMemory() w/ export" );
    }
    else if constexpr ( mem_type == ExternalMemory::Import )
    {
        if ( import_image_fd < 0 )
        {
            spdlog::error( "Invalid file descriptor" );
            return false;
        }
        auto const export_image_memory_info = VkImportMemoryFdInfoKHR{
            .sType      = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
            .pNext      = nullptr,
            .handleType = external_memory_handle_type,
            .fd         = import_image_fd,
        };
        color_image_alloc_info.pNext = &export_image_memory_info;
        CHECK_VK( ::vkAllocateMemory(
            device,
            &color_image_alloc_info,
            nullptr,
            &image.color_image_memory
        ) );
        spdlog::debug( "vkAllocateMemory() w/ import" );
    }
    else
    {
        CHECK_VK( ::vkAllocateMemory(
            device,
            &color_image_alloc_info,
            nullptr,
            &image.color_image_memory
        ) );
        spdlog::debug( "vkAllocateMemory()" );
    }

    CHECK_VK( ::vkBindImageMemory( device, image.color_image, image.color_image_memory, 0U ) );

    auto const color_image_view_create_info = VkImageViewCreateInfo{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0U,
        .image            = image.color_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = color_format,
        .components       = VkComponentMapping{ },
        .subresourceRange = VkImageSubresourceRange{
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0U,
            .levelCount     = 1U,
            .baseArrayLayer = 0U,
            .layerCount     = 1U,
        },
    };
    CHECK_VK( ::vkCreateImageView(
        device,
        &color_image_view_create_info,
        nullptr,
        &image.color_image_view
    ) );
    spdlog::debug( "vkCreateImageView()" );

    return true;
}

template auto initialize(
    ImageData< ExternalMemory::None >&,
    VkPhysicalDevice const&,
    VkDevice const&,
    VkExtent3D,
    VkFormat,
    int32
) -> bool;
template auto initialize(
    ImageData< ExternalMemory::Export >&,
    VkPhysicalDevice const&,
    VkDevice const&,
    VkExtent3D,
    VkFormat,
    int32
) -> bool;
template auto initialize(
    ImageData< ExternalMemory::Import >&,
    VkPhysicalDevice const&,
    VkDevice const&,
    VkExtent3D,
    VkFormat,
    int32
) -> bool;

auto get_file_descriptor(
    int32&                file_descriptor,
    VkInstance const&     instance,
    VkDevice const&       device,
    VkDeviceMemory const& image_memory
) -> bool
{
    auto* const vkGetMemoryFdKHR = reinterpret_cast< PFN_vkGetMemoryFdKHR >(
        ::vkGetInstanceProcAddr( instance, "vkGetMemoryFdKHR" )
    );
    if ( nullptr == vkGetMemoryFdKHR )
    {
        spdlog::error( "vkGetInstanceProcAddr() failed" );
        return false;
    }

    auto const memory_info = VkMemoryGetFdInfoKHR{
        .sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .pNext      = nullptr,
        .memory     = image_memory,
        .handleType = external_memory_handle_type,
    };
    CHECK_VK( vkGetMemoryFdKHR( device, &memory_info, &file_descriptor ) );
    spdlog::debug( "vkGetMemoryFdKHR()" );

    return true;
}

template < ExternalMemory mem_type >
auto destroy( ImageData< mem_type >& image, VkDevice const& device ) -> void
{
    if ( nullptr != image.color_image_view )
    {
        ::vkDestroyImageView( device, image.color_image_view, nullptr );
        spdlog::debug( "vkDestroyImageView()" );
    }

    if ( nullptr != image.color_image_memory )
    {
        ::vkFreeMemory( device, image.color_image_memory, nullptr );
        spdlog::debug( "vkFreeMemory()" );
    }

    if ( nullptr != image.color_image )
    {
        ::vkDestroyImage( device, image.color_image, nullptr );
        spdlog::debug( "vkDestroyImage()" );
    }
}

template auto destroy( ImageData< ExternalMemory::None >&, VkDevice const& device ) -> void;
template auto destroy( ImageData< ExternalMemory::Export >&, VkDevice const& device ) -> void;
template auto destroy( ImageData< ExternalMemory::Import >&, VkDevice const& device ) -> void;

} // namespace ltb::vlk
