// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////

// project
#include "ltb/utils/args.hpp"
#include "ltb/vlk/check.hpp"
#include "ltb/vlk/render.hpp"

// https://stackoverflow.com/questions/61089060/vulkan-render-to-texture
// https://github.com/SaschaWillems/Vulkan/blob/master/examples/offscreen/offscreen.cpp

namespace ltb
{
namespace
{

constexpr auto max_frames_in_flight = uint32_t{ 2 };

} // namespace

class App
{
public:
    App( )                               = default;
    App( App const& )                    = delete;
    App( App&& )                         = delete;
    auto operator=( App const& ) -> App& = delete;
    auto operator=( App&& ) -> App&      = delete;
    ~App( );

    auto initialize( uint32 physical_device_index ) -> bool;

    auto run( ) -> bool;

private:
    // Vulkan data
    vlk::SetupData< vlk::AppType::Windowed > setup_ = { };

    vlk::PipelineData< vlk::Pipeline::Composite > composite_pipeline_ = { };
    vlk::OutputData< vlk::AppType::Windowed >     windowed_output_    = { };
    vlk::SyncData< vlk::AppType::Windowed >       windowed_sync_      = { };

    vlk::PipelineData< vlk::Pipeline::Triangle > triangle_pipeline_ = { };
    vlk::OutputData< vlk::AppType::Headless >    headless_output_   = { };

    VkSampler color_image_sampler_ = { };
};

App::~App( )
{
    if ( nullptr != color_image_sampler_ )
    {
        ::vkDestroySampler( setup_.device, color_image_sampler_, nullptr );
        spdlog::debug( "vkDestroySampler()" );
    }

    vlk::destroy( setup_, headless_output_ );
    vlk::destroy( setup_, triangle_pipeline_ );

    vlk::destroy( setup_, windowed_sync_ );
    vlk::destroy( setup_, windowed_output_ );
    vlk::destroy( setup_, composite_pipeline_ );

    vlk::destroy( setup_ );
}

auto App::initialize( uint32 const physical_device_index ) -> bool
{
    CHECK_TRUE( vlk::initialize( physical_device_index, setup_ ) );

    CHECK_TRUE( vlk::initialize<vlk::AppType::Windowed>( max_frames_in_flight, setup_.surface_format.format, setup_.device, composite_pipeline_ ) );
    CHECK_TRUE( vlk::initialize( setup_, composite_pipeline_, windowed_output_ ) );
    CHECK_TRUE( vlk::initialize(
        max_frames_in_flight,
        setup_.device,
        setup_.graphics_command_pool,
        windowed_sync_
    ) );

    CHECK_TRUE( vlk::initialize<vlk::AppType::Headless>( setup_.surface_format.format, setup_.device, triangle_pipeline_ ) );
    CHECK_TRUE( vlk::initialize(
        VkExtent3D{
            windowed_output_.framebuffer_size.width,
            windowed_output_.framebuffer_size.height,
            1U
        },
        vlk::ExternalMemory::No,
        setup_,
        triangle_pipeline_,
        headless_output_
    ) );

    auto physical_device_properties = VkPhysicalDeviceProperties{ };
    ::vkGetPhysicalDeviceProperties( setup_.physical_device, &physical_device_properties );

    auto const sampler_info = VkSamplerCreateInfo{
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0U,
        .magFilter               = VK_FILTER_LINEAR,
        .minFilter               = VK_FILTER_LINEAR,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias              = 0.0F,
        .anisotropyEnable        = VK_TRUE,
        .maxAnisotropy           = physical_device_properties.limits.maxSamplerAnisotropy,
        .compareEnable           = VK_FALSE,
        .compareOp               = VK_COMPARE_OP_ALWAYS,
        .minLod                  = 0.0F,
        .maxLod                  = 0.0F,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };
    CHECK_VK( ::vkCreateSampler( setup_.device, &sampler_info, nullptr, &color_image_sampler_ ) );

    for ( auto i = 0U; i < max_frames_in_flight; ++i )
    {
        auto const image_info = VkDescriptorImageInfo{
            .sampler     = color_image_sampler_,
            .imageView   = headless_output_.color_image_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        auto const descriptor_writes = std::array{
            VkWriteDescriptorSet{
                .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext            = nullptr,
                .dstSet           = composite_pipeline_.descriptor_sets[ i ],
                .dstBinding       = 0U,
                .dstArrayElement  = 0U,
                .descriptorCount  = 1U,
                .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo       = &image_info,
                .pBufferInfo      = nullptr,
                .pTexelBufferView = nullptr,
            },
        };
        ::vkUpdateDescriptorSets(
            setup_.device,
            static_cast< uint32_t >( descriptor_writes.size( ) ),
            descriptor_writes.data( ),
            0U,
            nullptr
        );
    }

    return true;
}

auto App::run( ) -> bool
{
    spdlog::info( "Running render loop..." );

    auto const start_time = std::chrono::steady_clock::now( );

    using FloatSeconds = std::chrono::duration< float, std::chrono::seconds::period >;
    auto constexpr angular_velocity_rps = 0.5F;

    auto should_exit = false;
    while ( !should_exit )
    {
        ::glfwPollEvents( );

        auto const current_duration = start_time - std::chrono::steady_clock::now( );
        auto const current_duration_s
            = std::chrono::duration_cast< FloatSeconds >( current_duration ).count( );

        triangle_pipeline_.model_uniforms.scale_rotation_translation[ 1 ]
            = M_PI_2f * angular_velocity_rps * current_duration_s;

        // ///////////////////////////
        auto* const graphics_queue_fence
            = windowed_sync_.graphics_queue_fences[ windowed_sync_.current_frame ];
        auto const graphics_fences = std::array{ graphics_queue_fence };

        CHECK_VK( ::vkWaitForFences(
            setup_.device,
            static_cast< uint32 >( graphics_fences.size( ) ),
            graphics_fences.data( ),
            VK_TRUE,
            vlk::max_possible_timeout
        ) );

        auto        swapchain_image_index = uint32{ 0 };
        auto* const command_buffer = windowed_sync_.command_buffers[ windowed_sync_.current_frame ];

        CHECK_VK( ::vkAcquireNextImageKHR(
            setup_.device,
            windowed_output_.swapchain,
            vlk::max_possible_timeout,
            windowed_sync_.image_available_semaphores[ windowed_sync_.current_frame ],
            nullptr,
            &swapchain_image_index
        ) );

        CHECK_VK( ::vkResetFences(
            setup_.device,
            static_cast< uint32 >( graphics_fences.size( ) ),
            graphics_fences.data( )
        ) );

        auto constexpr reset_flags = VkCommandBufferResetFlags{ 0U };
        CHECK_VK( ::vkResetCommandBuffer( command_buffer, reset_flags ) );

        auto const begin_info = VkCommandBufferBeginInfo{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = 0U,
            .pInheritanceInfo = nullptr,
        };
        CHECK_VK( ::vkBeginCommandBuffer( command_buffer, &begin_info ) );

# if 1
        {
            auto const clear_values = std::array{
                VkClearValue{
                    .color = VkClearColorValue{ .float32 = { 0.0F, 0.0F, 0.0F, 0.0F } },
                },
            };
            auto const render_pass_info = VkRenderPassBeginInfo{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext           = nullptr,
            .renderPass      = triangle_pipeline_.render_pass,
            .framebuffer     = headless_output_.framebuffer,
            .renderArea      = VkRect2D{
                 .offset = VkOffset2D{ .x = 0, .y = 0 },
                 .extent = headless_output_.framebuffer_size,
            },
            .clearValueCount = static_cast< uint32 >( clear_values.size( ) ),
            .pClearValues    = clear_values.data( ),
        };
            ::vkCmdBeginRenderPass( command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE );
            ::vkCmdBindPipeline(
                command_buffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                triangle_pipeline_.pipeline
            );

            auto const viewport = VkViewport{
                .x        = 0.0F,
                .y        = 0.0F,
                .width    = static_cast< float32 >( headless_output_.framebuffer_size.width ),
                .height   = static_cast< float32 >( headless_output_.framebuffer_size.height ),
                .minDepth = 0.0F,
                .maxDepth = 1.0F,
            };
            auto constexpr first_viewport = 0U;
            auto constexpr viewport_count = 1U;
            ::vkCmdSetViewport( command_buffer, first_viewport, viewport_count, &viewport );

            auto const scissors = VkRect2D{
                .offset = VkOffset2D{ .x = 0, .y = 0 },
                .extent = headless_output_.framebuffer_size,
            };
            auto constexpr first_scissor = 0U;
            auto constexpr scissor_count = 1U;
            ::vkCmdSetScissor( command_buffer, first_scissor, scissor_count, &scissors );

            ::vkCmdPushConstants(
                command_buffer,
                triangle_pipeline_.pipeline_layout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0U,
                sizeof( triangle_pipeline_.model_uniforms ),
                &triangle_pipeline_.model_uniforms
            );

            ::vkCmdPushConstants(
                command_buffer,
                triangle_pipeline_.pipeline_layout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                sizeof( triangle_pipeline_.model_uniforms ),
                sizeof( triangle_pipeline_.display_uniforms ),
                &triangle_pipeline_.display_uniforms
            );

            auto constexpr vertex_count   = 3U;
            auto constexpr instance_count = 1U;
            auto constexpr first_vertex   = 0U;
            auto constexpr first_instance = 0U;
            ::vkCmdDraw(
                command_buffer,
                vertex_count,
                instance_count,
                first_vertex,
                first_instance
            );

            ::vkCmdEndRenderPass( command_buffer );
        }
#endif

        {
            auto const clear_values = std::array{
                VkClearValue{
                    .color = VkClearColorValue{ .float32 = { 0.0F, 0.0F, 0.0F, 0.0F } },
                },
            };
            auto const render_pass_info = VkRenderPassBeginInfo{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext           = nullptr,
            .renderPass      = composite_pipeline_.render_pass,
            .framebuffer     = windowed_output_.framebuffers[ swapchain_image_index ],
            .renderArea      = VkRect2D{
                 .offset = VkOffset2D{ .x = 0, .y = 0 },
                 .extent = windowed_output_.framebuffer_size,
            },
            .clearValueCount = static_cast< uint32 >( clear_values.size( ) ),
            .pClearValues    = clear_values.data( ),
        };
            ::vkCmdBeginRenderPass( command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE );
            ::vkCmdBindPipeline(
                command_buffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                composite_pipeline_.pipeline
            );

            auto const viewport = VkViewport{
                .x        = 0.0F,
                .y        = 0.0F,
                .width    = static_cast< float32 >( windowed_output_.framebuffer_size.width ),
                .height   = static_cast< float32 >( windowed_output_.framebuffer_size.height ),
                .minDepth = 0.0F,
                .maxDepth = 1.0F,
            };
            auto constexpr first_viewport = 0U;
            auto constexpr viewport_count = 1U;
            ::vkCmdSetViewport( command_buffer, first_viewport, viewport_count, &viewport );

            auto const scissors = VkRect2D{
                .offset = VkOffset2D{ .x = 0, .y = 0 },
                .extent = windowed_output_.framebuffer_size,
            };
            auto constexpr first_scissor = 0U;
            auto constexpr scissor_count = 1U;
            ::vkCmdSetScissor( command_buffer, first_scissor, scissor_count, &scissors );

            auto constexpr first_set            = 0U;
            auto constexpr descriptor_set_count = 1U;
            auto constexpr dynamic_offset_count = 0U;
            auto constexpr dynamic_offsets      = nullptr;
            ::vkCmdBindDescriptorSets(
                command_buffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                composite_pipeline_.pipeline_layout,
                first_set,
                descriptor_set_count,
                &composite_pipeline_.descriptor_sets[ windowed_sync_.current_frame ],
                dynamic_offset_count,
                dynamic_offsets
            );

            auto constexpr vertex_count   = 4U;
            auto constexpr instance_count = 1U;
            auto constexpr first_vertex   = 0U;
            auto constexpr first_instance = 0U;
            ::vkCmdDraw(
                command_buffer,
                vertex_count,
                instance_count,
                first_vertex,
                first_instance
            );

            ::vkCmdEndRenderPass( command_buffer );
        }

        CHECK_VK( ::vkEndCommandBuffer( command_buffer ) );

        auto constexpr semaphore_stage
            = VkPipelineStageFlags{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        auto const submit_info = VkSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            // Color attachment stage must wait for the image acquisition to finish.
            .waitSemaphoreCount = 1U,
            .pWaitSemaphores
            = &windowed_sync_.image_available_semaphores[ windowed_sync_.current_frame ],
            .pWaitDstStageMask = &semaphore_stage,
            // The commands being submitted to the graphics device.
            .commandBufferCount = 1U,
            .pCommandBuffers    = &command_buffer,
            // Signal the render finished semaphore so future
            // commands waiting on this step can proceed.
            .signalSemaphoreCount = 1U,
            .pSignalSemaphores
            = &windowed_sync_.render_finished_semaphores[ windowed_sync_.current_frame ],
        };

        // Use the fence to block future CPU code that also references this fence.
        auto constexpr submit_count = 1U;
        CHECK_VK( ::vkQueueSubmit(
            setup_.graphics_queue,
            submit_count,
            &submit_info,
            graphics_queue_fence
        ) );

        auto const present_info = VkPresentInfoKHR{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            // Wait for the render to finish before presenting.
            .waitSemaphoreCount = 1U,
            .pWaitSemaphores
            = &windowed_sync_.render_finished_semaphores[ windowed_sync_.current_frame ],
            .swapchainCount = 1U,
            .pSwapchains    = &windowed_output_.swapchain,
            .pImageIndices  = &swapchain_image_index,
            .pResults       = nullptr,
        };
        CHECK_VK( ::vkQueuePresentKHR( setup_.surface_queue, &present_info ) );

        // // Render offline triangle.
        // CHECK_TRUE( vlk::render( setup_, triangle_pipeline_, headless_output_, headless_sync_ ) );
        //
        // // Render pipeline here.
        // CHECK_TRUE( vlk::render( setup_, composite_pipeline_, windowed_output_, windowed_sync_ ) );

        windowed_sync_.current_frame = ( windowed_sync_.current_frame + 1U ) % max_frames_in_flight;

        // This GLFW_KEY_ESCAPE bit shouldn't exist in a final product.
        should_exit = ( GLFW_TRUE == ::glfwWindowShouldClose( setup_.window ) )
                   || ( GLFW_PRESS == ::glfwGetKey( setup_.window, GLFW_KEY_ESCAPE ) );
    }

    CHECK_VK( ::vkDeviceWaitIdle( setup_.device ) );

    spdlog::info( "Exiting..." );
    return true;
}

} // namespace ltb

auto main( ltb::int32 const argc, char const* argv[] ) -> ltb::int32
{
    spdlog::set_level( spdlog::level::debug );

    auto physical_device_index = ltb::uint32{ 0 };
    if ( !ltb::utils::get_physical_device_index_from_args(
             { argv, static_cast< size_t >( argc ) },
             physical_device_index
         ) )
    {
        return EXIT_FAILURE;
    }

    if ( auto app = ltb::App( ); app.initialize( physical_device_index ) && app.run( ) )
    {
        spdlog::info( "Done." );
        return EXIT_SUCCESS;
    }
    else
    {
        return EXIT_FAILURE;
    }
}
