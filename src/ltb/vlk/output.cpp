// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#include "ltb/vlk/output.hpp"

// standard
#include <array>
#include <set>
#include <vector>

namespace ltb::vlk
{

namespace
{

template < AppType app_type >
auto initialize_render_pass(
    VkRenderPass&   render_pass,
    VkFormat const  color_format,
    VkDevice const& device
)
{
    auto final_layout = VkImageLayout{ };
    if constexpr ( app_type == AppType::Windowed )
    {
        final_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    else
    {
        final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    auto const attachments = std::vector{
        VkAttachmentDescription{
            .flags          = 0U,
            .format         = color_format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = final_layout,
        },
    };

    auto const color_attachment_refs = std::vector{
        VkAttachmentReference{
            .attachment = 0U,
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
    };

    auto const subpasses = std::vector{
        VkSubpassDescription{
            .flags                   = 0U,
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount    = 0U,
            .pInputAttachments       = nullptr,
            .colorAttachmentCount    = static_cast< uint32 >( color_attachment_refs.size( ) ),
            .pColorAttachments       = color_attachment_refs.data( ),
            .pResolveAttachments     = nullptr,
            .pDepthStencilAttachment = nullptr,
            .preserveAttachmentCount = 0U,
            .pPreserveAttachments    = nullptr,
        },
    };

    auto subpass_dependencies = std::vector< VkSubpassDependency >{ };

    if constexpr ( app_type == AppType::Headless )
    {
        subpass_dependencies = {
            {
                .srcSubpass      = VK_SUBPASS_EXTERNAL,
                .dstSubpass      = 0U,
                .srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask   = VK_ACCESS_NONE_KHR,
                .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
            },
            {
                .srcSubpass      = 0U,
                .dstSubpass      = VK_SUBPASS_EXTERNAL,
                .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                .dstAccessMask   = VK_ACCESS_MEMORY_READ_BIT,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
            },
        };
    }
    else
    {
        subpass_dependencies = {
            {
                .srcSubpass      = VK_SUBPASS_EXTERNAL,
                .dstSubpass      = 0U,
                .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask   = 0U,
                .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dependencyFlags = 0U,
            },
        };
    }

    auto const render_pass_create_info = VkRenderPassCreateInfo{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0U,
        .attachmentCount = static_cast< uint32 >( attachments.size( ) ),
        .pAttachments    = attachments.data( ),
        .subpassCount    = static_cast< uint32 >( subpasses.size( ) ),
        .pSubpasses      = subpasses.data( ),
        .dependencyCount = static_cast< uint32 >( subpass_dependencies.size( ) ),
        .pDependencies   = subpass_dependencies.data( ),
    };
    CHECK_VK( ::vkCreateRenderPass( device, &render_pass_create_info, nullptr, &render_pass ) );
    spdlog::debug( "vkCreateRenderPass()" );

    return true;
}

} // namespace

auto initialize(
    OutputData< AppType::Windowed >& output,
    GLFWwindow* const                window,
    VkSurfaceKHR const&              surface,
    VkPhysicalDevice const&          physical_device,
    VkDevice const&                  device,
    uint32 const                     graphics_queue_family_index,
    uint32 const                     surface_queue_family_index,
    VkSurfaceFormatKHR const&        surface_format
) -> bool
{
    CHECK_TRUE( initialize_render_pass<
                AppType::Windowed >( output.render_pass, surface_format.format, device ) );

    auto framebuffer_width  = int32{ 0 };
    auto framebuffer_height = int32{ 0 };
    ::glfwGetFramebufferSize( window, &framebuffer_width, &framebuffer_height );

    auto surface_capabilities = VkSurfaceCapabilitiesKHR{ };
    CHECK_VK( ::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device,
        surface,
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
        graphics_queue_family_index,
        surface_queue_family_index,
    };

    auto const queue_family_indices
        = std::vector< uint32 >( unique_queue_indices.begin( ), unique_queue_indices.end( ) );

    auto const concurrency               = ( queue_family_indices.size( ) > 1 );
    auto const unique_queue_family_count = static_cast< uint32 >( queue_family_indices.size( ) );

    auto const swapchain_create_info = VkSwapchainCreateInfoKHR{
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext            = nullptr,
        .flags            = 0U,
        .surface          = surface,
        .minImageCount    = min_image_count,
        .imageFormat      = surface_format.format,
        .imageColorSpace  = surface_format.colorSpace,
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
    CHECK_VK( ::vkCreateSwapchainKHR( device, &swapchain_create_info, nullptr, &output.swapchain )
    );
    spdlog::debug( "vkCreateSwapchainKHR()" );

    auto swapchain_image_count = uint32{ 0 };
    CHECK_VK( ::vkGetSwapchainImagesKHR( device, output.swapchain, &swapchain_image_count, nullptr )
    );
    output.swapchain_images.resize( swapchain_image_count );
    CHECK_VK( ::vkGetSwapchainImagesKHR(
        device,
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
            .format           = surface_format.format,
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
            device,
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
            .renderPass      = output.render_pass,
            .attachmentCount = 1U,
            .pAttachments    = output.swapchain_image_views.data( ) + i,
            .width           = output.framebuffer_size.width,
            .height          = output.framebuffer_size.height,
            .layers          = 1U,
        };
        CHECK_VK( ::vkCreateFramebuffer(
            device,
            &framebuffer_create_info,
            nullptr,
            output.framebuffers.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateFramebuffer()x{}", output.framebuffers.size( ) );

    return true;
}

auto initialize(
    OutputData< AppType::Headless >& output,
    VkDevice const&                  device,
    VkImageView const&               color_image_view,
    VkExtent3D const&                image_extent,
    VkFormat const                   color_format
) -> bool
{
    CHECK_TRUE(
        initialize_render_pass< AppType::Windowed >( output.render_pass, color_format, device )
    );

    auto const framebuffer_create_info = VkFramebufferCreateInfo{
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0U,
        .renderPass      = output.render_pass,
        .attachmentCount = 1U,
        .pAttachments    = &color_image_view,
        .width           = image_extent.width,
        .height          = image_extent.height,
        .layers          = 1U,
    };
    CHECK_VK(
        ::vkCreateFramebuffer( device, &framebuffer_create_info, nullptr, &output.framebuffer )
    );
    spdlog::debug( "vkCreateFramebuffer()" );

    return true;
}

auto initialize(
    OutputData< AppType::Windowed >&      output,
    SetupData< AppType::Windowed > const& setup
) -> bool
{
    return initialize(
        output,
        setup.window,
        setup.surface,
        setup.physical_device,
        setup.device,
        setup.graphics_queue_family_index,
        setup.surface_queue_family_index,
        setup.surface_format
    );
}

template < AppType app_type >
auto destroy( OutputData< app_type >& output, VkDevice const& device ) -> void
{
    if constexpr ( app_type == AppType::Windowed )
    {
        for ( auto* const framebuffer : output.framebuffers )
        {
            ::vkDestroyFramebuffer( device, framebuffer, nullptr );
        }
        spdlog::debug( "vkDestroyFramebuffer()x{}", output.framebuffers.size( ) );
        output.framebuffers.clear( );

        for ( auto* const image_view : output.swapchain_image_views )
        {
            ::vkDestroyImageView( device, image_view, nullptr );
        }
        spdlog::debug( "vkDestroyImageView()x{}", output.swapchain_image_views.size( ) );
        output.swapchain_image_views.clear( );

        output.swapchain_images.clear( );

        if ( nullptr != output.swapchain )
        {
            ::vkDestroySwapchainKHR( device, output.swapchain, nullptr );
            spdlog::debug( "vkDestroySwapchainKHR()" );
        }
    }
    else
    {
        if ( nullptr != output.framebuffer )
        {
            ::vkDestroyFramebuffer( device, output.framebuffer, nullptr );
            spdlog::debug( "vkDestroyFramebuffer()" );
        }
    }

    if ( nullptr != output.render_pass )
    {
        ::vkDestroyRenderPass( device, output.render_pass, nullptr );
        spdlog::debug( "vkDestroyRenderPass()" );
    }
}

template auto destroy( OutputData< AppType::Windowed >&, VkDevice const& ) -> void;
template auto destroy( OutputData< AppType::Headless >&, VkDevice const& ) -> void;

} // namespace ltb::vlk
