// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////

// project
#include "ltb/ltb_config.hpp"
#include "ltb/vlk/check.hpp"
#include "ltb/vlk/output.hpp"
#include "ltb/vlk/pipeline.hpp"
#include "ltb/vlk/synchronization.hpp"

// standard
#include <charconv>
#include <ranges>
#include <vector>

namespace ltb
{
namespace
{

constexpr auto max_frames_in_flight = uint32_t{ 2 };
constexpr auto max_possible_timeout = std::numeric_limits< uint64_t >::max( );

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
    vlk::SetupData< vlk::AppType::Windowed >     setup_             = { };
    vlk::PipelineData< vlk::Pipeline::Triangle > triangle_pipeline_ = { };
    vlk::OutputData< vlk::AppType::Windowed >    windowed_output_   = { };
    vlk::OutputData< vlk::AppType::Headless >    headless_output_   = { };
    vlk::SyncData< vlk::AppType::Windowed >      sync_              = { };
};

App::~App( )
{
    vlk::destroy( setup_, sync_ );
    vlk::destroy( setup_, triangle_pipeline_ );
    vlk::destroy( setup_, windowed_output_ );
    vlk::destroy( setup_ );
}

auto App::initialize( uint32 const physical_device_index ) -> bool
{
    CHECK_TRUE( vlk::initialize( physical_device_index, setup_ ) );
    CHECK_TRUE( vlk::initialize( setup_, triangle_pipeline_ ) );
    CHECK_TRUE( vlk::initialize( setup_, triangle_pipeline_, windowed_output_ ) );
    CHECK_TRUE( vlk::initialize(
        VkExtent3D{
            windowed_output_.framebuffer_size.width,
            windowed_output_.framebuffer_size.height,
            1
        },
        vlk::ExternalMemory::No,
        setup_,
        triangle_pipeline_,
        headless_output_
    ) );
    CHECK_TRUE( vlk::initialize( max_frames_in_flight, setup_, sync_ ) );
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

        // Render pipeline here.
        auto* const graphics_queue_fence = sync_.graphics_queue_fences[ sync_.current_frame ];

        auto const graphics_fences = std::array{ graphics_queue_fence };

        CHECK_VK( ::vkWaitForFences(
            setup_.device,
            static_cast< uint32 >( graphics_fences.size( ) ),
            graphics_fences.data( ),
            VK_TRUE,
            max_possible_timeout
        ) );

        auto* const image_available_semaphore
            = sync_.image_available_semaphores[ sync_.current_frame ];

        auto swapchain_image_index = uint32{ 0 };
        CHECK_VK( ::vkAcquireNextImageKHR(
            setup_.device,
            windowed_output_.swapchain,
            max_possible_timeout,
            image_available_semaphore,
            nullptr,
            &swapchain_image_index
        ) );
        auto* const framebuffer = windowed_output_.framebuffers[ swapchain_image_index ];

        CHECK_VK( ::vkResetFences(
            setup_.device,
            static_cast< uint32 >( graphics_fences.size( ) ),
            graphics_fences.data( )
        ) );

        auto* const command_buffer = sync_.command_buffers[ sync_.current_frame ];
        auto constexpr reset_flags = VkCommandBufferResetFlags{ 0U };
        CHECK_VK( ::vkResetCommandBuffer( command_buffer, reset_flags ) );

        auto const begin_info = VkCommandBufferBeginInfo{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .pInheritanceInfo = nullptr,
        };
        CHECK_VK( ::vkBeginCommandBuffer( command_buffer, &begin_info ) );

        auto const clear_values = std::array{
            VkClearValue{
                .color = VkClearColorValue{ .float32 = { 0.0F, 0.0F, 0.0F, 0.0F } },
            },
        };
        auto const render_pass_info = VkRenderPassBeginInfo{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext           = nullptr,
            .renderPass      = triangle_pipeline_.render_pass,
            .framebuffer     = framebuffer,
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
            triangle_pipeline_.pipeline
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

        ::vkCmdPushConstants(
            command_buffer,
            triangle_pipeline_.pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
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
        ::vkCmdDraw( command_buffer, vertex_count, instance_count, first_vertex, first_instance );

        ::vkCmdEndRenderPass( command_buffer );

        CHECK_VK( ::vkEndCommandBuffer( command_buffer ) );

        auto* const render_finished_semaphore
            = sync_.render_finished_semaphores[ sync_.current_frame ];
        auto constexpr semaphore_stage
            = VkPipelineStageFlags{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        auto const submit_info = VkSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            // Color attachment stage must wait for the image acquisition to finish.
            .waitSemaphoreCount = 1U,
            .pWaitSemaphores    = &image_available_semaphore,
            .pWaitDstStageMask  = &semaphore_stage,
            // The commands being submitted to the graphics device.
            .commandBufferCount = 1U,
            .pCommandBuffers    = &command_buffer,
            // Signal the render finished semaphore so future
            // commands waiting on this step can proceed.
            .signalSemaphoreCount = 1U,
            .pSignalSemaphores    = &render_finished_semaphore,
        };

        // Use the fence to block future CPU code that also references this fence.
        auto constexpr submit_count = 1;
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
            .pWaitSemaphores    = &render_finished_semaphore,
            .swapchainCount     = 1U,
            .pSwapchains        = &windowed_output_.swapchain,
            .pImageIndices      = &swapchain_image_index,
            .pResults           = nullptr,
        };
        CHECK_VK( ::vkQueuePresentKHR( setup_.surface_queue, &present_info ) );

        sync_.current_frame = ( sync_.current_frame + 1U ) % max_frames_in_flight;

        // This GLFW_KEY_ESCAPE bit shouldn't exist in a final product.
        should_exit = ( GLFW_TRUE == ::glfwWindowShouldClose( setup_.window ) )
                   || ( GLFW_PRESS == ::glfwGetKey( setup_.window, GLFW_KEY_ESCAPE ) );
    }

    CHECK_VK( ::vkDeviceWaitIdle( setup_.device ) );

    spdlog::info( "Exiting..." );
    return true;
}

} // namespace ltb

auto main( ltb::int32 const argc, char const* const argv[] ) -> ltb::int32
{
    spdlog::set_level( spdlog::level::debug );

    auto physical_device_index = ltb::uint32{ 0 };
    if ( argc > 1 )
    {
        auto const* const start = argv[ 1 ];
        auto const* const end   = argv[ 1 ] + std::strlen( argv[ 1 ] );

        if ( auto const result = std::from_chars( start, end, physical_device_index );
             std::errc( ) != result.ec )
        {
            spdlog::error( "Invalid argument: {}", argv[ 1 ] );
            return EXIT_FAILURE;
        }
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
