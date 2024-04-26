// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#include "ltb/vlk/output.hpp"

// project
#include "ltb/vlk/check.hpp"

// external
#include <spdlog/spdlog.h>

// standard
#include <set>

namespace ltb::vlk
{
namespace
{

auto constexpr external_memory_handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

} // namespace

template < Pipeline pipeline_type >
auto initialize(
    SetupData< AppType::Windowed > const& setup,
    PipelineData< pipeline_type > const&  pipeline,
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
            .renderPass      = pipeline.render_pass,
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

template auto
initialize( SetupData< AppType::Windowed > const&, PipelineData< Pipeline::Triangle > const&, OutputData< AppType::Windowed >& )
    -> bool;
template auto
initialize( SetupData< AppType::Windowed > const&, PipelineData< Pipeline::Composite > const&, OutputData< AppType::Windowed >& )
    -> bool;

} // namespace ltb::vlk
