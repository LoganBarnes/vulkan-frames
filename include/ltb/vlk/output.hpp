// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/vlk/pipeline.hpp"

// standard
#include <optional>
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

template < AppType app_type, Pipeline pipeline_type >
auto initialize(
    VkExtent3D                           image_extents,
    ExternalMemory                       external_memory,
    SetupData< app_type > const&         setup,
    PipelineData< pipeline_type > const& pipeline,
    OutputData< AppType::Headless >&     output
) -> bool;

template < Pipeline pipeline_type >
auto initialize(
    SetupData< AppType::Windowed > const& setup,
    PipelineData< pipeline_type > const&  pipeline,
    OutputData< AppType::Windowed >&      output
) -> bool;

template < AppType app_type >
auto destroy( SetupData< app_type > const& setup, OutputData< app_type >& output ) -> void;

template < AppType app_type, Pipeline pipeline_type >
auto initialize(
    VkExtent3D                           image_extents,
    ExternalMemory                       external_memory,
    SetupData< app_type > const&         setup,
    PipelineData< pipeline_type > const& pipeline,
    OutputData< AppType::Headless >&     output
) -> bool
{
    auto constexpr external_memory_handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    output.framebuffer_size = VkExtent2D{ image_extents.width, image_extents.height };

    auto color_format = VkFormat{ };
    if constexpr ( AppType::Headless == app_type )
    {
        color_format = setup.color_format;
    }
    else
    {
        color_format = setup.surface_format.format;
    }

    auto const external_color_image_info = VkExternalMemoryImageCreateInfo{
        .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext       = nullptr,
        .handleTypes = external_memory_handle_type,
    };
    auto const color_image_create_info = VkImageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = ( ExternalMemory::Yes == external_memory ? &external_color_image_info : nullptr ),
        .flags = 0,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = color_format,
        .extent      = image_extents,
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
        .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    CHECK_VK(
        ::vkCreateImage( setup.device, &color_image_create_info, nullptr, &output.color_image )
    );
    spdlog::debug( "vkCreateImage()" );

    auto color_image_mem_reqs = VkMemoryRequirements{ };
    ::vkGetImageMemoryRequirements( setup.device, output.color_image, &color_image_mem_reqs );

    auto memory_props = VkPhysicalDeviceMemoryProperties{ };
    ::vkGetPhysicalDeviceMemoryProperties( setup.physical_device, &memory_props );
    auto const mem_prop_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    auto memory_type_index = std::optional< uint32 >{ };
    for ( auto i = 0u; i < memory_props.memoryTypeCount; ++i )
    {
        auto const type_is_suitable
            = ( 0 != ( color_image_mem_reqs.memoryTypeBits & ( 1u << i ) ) );
        auto const props_exist
            = ( memory_props.memoryTypes[ i ].propertyFlags & mem_prop_flags ) == mem_prop_flags;

        if ( ( !memory_type_index ) && type_is_suitable && props_exist )
        {
            memory_type_index = uint32{ i };
        }
    }

    if ( !memory_type_index )
    {
        spdlog::error( "No suitable memory type found" );
        return false;
    }

    auto const export_image_memory_info = VkExportMemoryAllocateInfo{
        .sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext       = nullptr,
        .handleTypes = external_memory_handle_type,
    };
    auto const color_image_alloc_info = VkMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = ( ExternalMemory::Yes == external_memory ? &export_image_memory_info : nullptr ),
        .allocationSize  = color_image_mem_reqs.size,
        .memoryTypeIndex = memory_type_index.value( ),
    };
    CHECK_VK( ::vkAllocateMemory(
        setup.device,
        &color_image_alloc_info,
        nullptr,
        &output.color_image_memory
    ) );
    spdlog::debug( "vkAllocateMemory()" );

    CHECK_VK( ::vkBindImageMemory( setup.device, output.color_image, output.color_image_memory, 0 )
    );

    auto const color_image_view_create_info = VkImageViewCreateInfo{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .image            = output.color_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = color_format,
        .components       = VkComponentMapping{ },
        .subresourceRange = VkImageSubresourceRange{
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    CHECK_VK( ::vkCreateImageView(
        setup.device,
        &color_image_view_create_info,
        nullptr,
        &output.color_image_view
    ) );
    spdlog::debug( "vkCreateImageView()" );

    auto const framebuffer_create_info = VkFramebufferCreateInfo{
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0,
        .renderPass      = pipeline.render_pass,
        .attachmentCount = 1,
        .pAttachments    = &output.color_image_view,
        .width           = image_extents.width,
        .height          = image_extents.height,
        .layers          = 1,
    };
    CHECK_VK( ::vkCreateFramebuffer(
        setup.device,
        &framebuffer_create_info,
        nullptr,
        &output.framebuffer
    ) );
    spdlog::debug( "vkCreateFramebuffer()" );

    return true;
}

} // namespace ltb::vlk
