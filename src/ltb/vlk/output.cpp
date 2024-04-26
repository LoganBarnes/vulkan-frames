// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#include "ltb/vlk/output.hpp"

// project
#include "ltb/vlk/check.hpp"

// external
#include <spdlog/spdlog.h>

// standard
#include <optional>
#include <set>

namespace ltb::vlk
{
namespace
{

auto constexpr external_memory_handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

} // namespace

auto initialize(
    VkExtent3D                            image_extents,
    ExternalMemory                        external_memory,
    SetupData< AppType::Headless > const& setup,
    OutputData< AppType::Headless >&      output
) -> bool
{
    output.framebuffer_size = VkExtent2D{ image_extents.width, image_extents.height };

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
        .format      = setup.color_format,
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
        .format           = setup.color_format,
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
        .renderPass      = setup.render_pass,
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

auto initialize(
    SetupData< AppType::Windowed > const& setup,
    OutputData< AppType::Windowed >&      output
) -> bool
{
    auto framebuffer_width  = int32{ 0 };
    auto framebuffer_height = int32{ 0 };
    ::glfwGetFramebufferSize( setup.window, &framebuffer_width, &framebuffer_height );

    auto surface_capabilities = VkSurfaceCapabilitiesKHR{ };
    CHECK_VK( ::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        setup.physical_device,
        setup.surface,
        &surface_capabilities
    ) );

    output.framebuffer_size = VkExtent2D{
        std::clamp(
            static_cast< uint32 >( framebuffer_width ),
            surface_capabilities.minImageExtent.width,
            surface_capabilities.maxImageExtent.width
        ),
        std::clamp(
            static_cast< uint32 >( framebuffer_height ),
            surface_capabilities.minImageExtent.height,
            surface_capabilities.maxImageExtent.height
        ),
    };

    auto min_image_count = surface_capabilities.minImageCount + 1U;

    // don't exceed the max (zero means no maximum).
    if ( ( surface_capabilities.maxImageCount > 0U )
         && ( min_image_count > surface_capabilities.maxImageCount ) )
    {
        min_image_count = surface_capabilities.maxImageCount;
    }

    auto const unique_queue_indices = std::set{
        setup.graphics_queue_family_index,
        setup.surface_queue_family_index,
    };

    auto const queue_family_indices
        = std::vector< uint32 >( unique_queue_indices.begin( ), unique_queue_indices.end( ) );

    auto const concurrency               = ( queue_family_indices.size( ) > 1 );
    auto const unique_queue_family_count = static_cast< uint32 >( queue_family_indices.size( ) );

    auto const swapchain_create_info = VkSwapchainCreateInfoKHR{
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext            = nullptr,
        .flags            = 0U,
        .surface          = setup.surface,
        .minImageCount    = min_image_count,
        .imageFormat      = setup.surface_format.format,
        .imageColorSpace  = setup.surface_format.colorSpace,
        .imageExtent      = output.framebuffer_size,
        .imageArrayLayers = 1U,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode
        = ( concurrency ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE ),
        .queueFamilyIndexCount = ( concurrency ? unique_queue_family_count : 0 ),
        .pQueueFamilyIndices   = queue_family_indices.data( ),
        .preTransform          = surface_capabilities.currentTransform,
        .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode           = VK_PRESENT_MODE_FIFO_KHR,
        .clipped               = VK_TRUE,
        .oldSwapchain          = nullptr,
    };
    CHECK_VK(
        ::vkCreateSwapchainKHR( setup.device, &swapchain_create_info, nullptr, &output.swapchain )
    );
    spdlog::debug( "vkCreateSwapchainKHR()" );

    auto swapchain_image_count = uint32{ 0 };
    CHECK_VK(
        ::vkGetSwapchainImagesKHR( setup.device, output.swapchain, &swapchain_image_count, nullptr )
    );
    output.swapchain_images.resize( swapchain_image_count );
    CHECK_VK( ::vkGetSwapchainImagesKHR(
        setup.device,
        output.swapchain,
        &swapchain_image_count,
        output.swapchain_images.data( )
    ) );
    spdlog::debug( "vkGetSwapchainImagesKHR()" );

    output.swapchain_image_views.resize( swapchain_image_count );
    for ( auto i = 0U; i < swapchain_image_count; ++i )
    {
        auto const image_view_create_info = VkImageViewCreateInfo{
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = 0U,
            .image            = output.swapchain_images[ i ],
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = setup.surface_format.format,
            .components       = VkComponentMapping{
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = VkImageSubresourceRange{
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0U,
                .levelCount     = 1U,
                .baseArrayLayer = 0U,
                .layerCount     = 1U,
            },
        };
        CHECK_VK( ::vkCreateImageView(
            setup.device,
            &image_view_create_info,
            nullptr,
            output.swapchain_image_views.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateImageView()x{}", output.swapchain_image_views.size( ) );

    output.framebuffers.resize( swapchain_image_count );
    for ( auto i = 0U; i < swapchain_image_count; ++i )
    {
        auto const framebuffer_create_info = VkFramebufferCreateInfo{
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = nullptr,
            .flags           = 0U,
            .renderPass      = setup.render_pass,
            .attachmentCount = 1U,
            .pAttachments    = output.swapchain_image_views.data( ) + i,
            .width           = output.framebuffer_size.width,
            .height          = output.framebuffer_size.height,
            .layers          = 1U,
        };
        CHECK_VK( ::vkCreateFramebuffer(
            setup.device,
            &framebuffer_create_info,
            nullptr,
            output.framebuffers.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateFramebuffer()x{}", output.framebuffers.size( ) );

    return true;
}

template <>
auto destroy( SetupData< AppType::Headless > const& setup, OutputData< AppType::Headless >& output )
    -> void
{
    if ( nullptr != output.framebuffer )
    {
        ::vkDestroyFramebuffer( setup.device, output.framebuffer, nullptr );
        spdlog::debug( "vkDestroyFramebuffer()" );
    }

    if ( nullptr != output.color_image_view )
    {
        ::vkDestroyImageView( setup.device, output.color_image_view, nullptr );
        spdlog::debug( "vkDestroyImageView()" );
    }

    if ( nullptr != output.color_image_memory )
    {
        ::vkFreeMemory( setup.device, output.color_image_memory, nullptr );
        spdlog::debug( "vkFreeMemory()" );
    }

    if ( nullptr != output.color_image )
    {
        ::vkDestroyImage( setup.device, output.color_image, nullptr );
        spdlog::debug( "vkDestroyImage()" );
    }
}

template <>
auto destroy( SetupData< AppType::Windowed > const& setup, OutputData< AppType::Windowed >& output )
    -> void
{
    for ( auto* const framebuffer : output.framebuffers )
    {
        ::vkDestroyFramebuffer( setup.device, framebuffer, nullptr );
    }
    spdlog::debug( "vkDestroyFramebuffer()x{}", output.framebuffers.size( ) );
    output.framebuffers.clear( );

    for ( auto* const image_view : output.swapchain_image_views )
    {
        ::vkDestroyImageView( setup.device, image_view, nullptr );
    }
    spdlog::debug( "vkDestroyImageView()x{}", output.swapchain_image_views.size( ) );
    output.swapchain_image_views.clear( );

    output.swapchain_images.clear( );

    if ( nullptr != output.swapchain )
    {
        ::vkDestroySwapchainKHR( setup.device, output.swapchain, nullptr );
        spdlog::debug( "vkDestroySwapchainKHR()" );
    }
}

} // namespace ltb::vlk
