// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////

// project
#include "ltb/ltb_config.hpp"
#include "ltb/vlk/check.hpp"
#include "ltb/vlk/output.hpp"
#include "ltb/vlk/pipeline.hpp"

// standard
#include <charconv>
#include <ranges>
#include <vector>

namespace ltb
{
namespace
{

auto constexpr image_extents = VkExtent3D{
    .width  = 1920,
    .height = 1080,
    .depth  = 1,
};

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
    // Common setup objects
    vlk::SetupData< vlk::AppType::Windowed > setup_ = { };

    // Pools
    VkDescriptorPool descriptor_pool_ = { };

    // Synchronization objects
    std::vector< VkCommandBuffer > command_buffers_            = { };
    std::vector< VkSemaphore >     image_available_semaphores_ = { };
    std::vector< VkSemaphore >     render_finished_semaphores_ = { };
    std::vector< VkFence >         graphics_queue_fences_      = { };
    uint32                         current_frame_              = 0;

    // Output
    vlk::OutputData< vlk::AppType::Windowed > output_ = { };

    // Pipeline
    vlk::PipelineData< vlk::Pipeline::Triangle > pipeline_ = { };
};

App::~App( )
{
    vlk::destroy( setup_, pipeline_ );
    vlk::destroy( setup_, output_ );

    for ( auto* const fence : graphics_queue_fences_ )
    {
        ::vkDestroyFence( setup_.device, fence, nullptr );
    }
    spdlog::debug( "vkDestroyFence()x{}", graphics_queue_fences_.size( ) );
    graphics_queue_fences_.clear( );

    for ( auto* const semaphore : render_finished_semaphores_ )
    {
        ::vkDestroySemaphore( setup_.device, semaphore, nullptr );
    }
    spdlog::debug( "vkDestroySemaphore()x{}", render_finished_semaphores_.size( ) );
    render_finished_semaphores_.clear( );

    for ( auto* const semaphore : image_available_semaphores_ )
    {
        ::vkDestroySemaphore( setup_.device, semaphore, nullptr );
    }
    spdlog::debug( "vkDestroySemaphore()x{}", image_available_semaphores_.size( ) );
    image_available_semaphores_.clear( );

    if ( !command_buffers_.empty( ) )
    {
        ::vkFreeCommandBuffers(
            setup_.device,
            setup_.graphics_command_pool,
            static_cast< uint32 >( command_buffers_.size( ) ),
            command_buffers_.data( )
        );
        spdlog::debug( "vkFreeCommandBuffers()" );
    }

    if ( nullptr != descriptor_pool_ )
    {
        ::vkDestroyDescriptorPool( setup_.device, descriptor_pool_, nullptr );
        spdlog::debug( "vkDestroyDescriptorPool()" );
    }

    vlk::destroy( setup_ );
}

auto App::initialize( uint32 const physical_device_index ) -> bool
{
    CHECK_TRUE( vlk::initialize( setup_, physical_device_index ) );

    auto const cmd_buf_alloc_info = VkCommandBufferAllocateInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = setup_.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = max_frames_in_flight,
    };
    command_buffers_.resize( cmd_buf_alloc_info.commandBufferCount );
    CHECK_VK(
        ::vkAllocateCommandBuffers( setup_.device, &cmd_buf_alloc_info, command_buffers_.data( ) )
    );
    spdlog::debug( "vkAllocateCommandBuffers()" );

    auto const semaphore_create_info = VkSemaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
    };

    image_available_semaphores_.resize( max_frames_in_flight );
    render_finished_semaphores_.resize( max_frames_in_flight );

    for ( auto i = 0U; i < max_frames_in_flight; ++i )
    {
        CHECK_VK( ::vkCreateSemaphore(
            setup_.device,
            &semaphore_create_info,
            nullptr,
            image_available_semaphores_.data( ) + i
        ) );
        CHECK_VK( ::vkCreateSemaphore(
            setup_.device,
            &semaphore_create_info,
            nullptr,
            render_finished_semaphores_.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateSemaphore()x{}", max_frames_in_flight );
    spdlog::debug( "vkCreateSemaphore()x{}", max_frames_in_flight );

    auto const fence_create_info = VkFenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    graphics_queue_fences_.resize( max_frames_in_flight );
    for ( auto i = 0U; i < max_frames_in_flight; ++i )
    {
        CHECK_VK( ::vkCreateFence(
            setup_.device,
            &fence_create_info,
            nullptr,
            graphics_queue_fences_.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateFence()x{}", max_frames_in_flight );

    CHECK_TRUE( vlk::initialize( setup_, output_ ) );
    CHECK_TRUE( vlk::initialize( setup_, pipeline_ ) );

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

        pipeline_.model_uniforms.scale_rotation_translation[ 1 ]
            = M_PI_2f * angular_velocity_rps * current_duration_s;

        // Render pipeline here.
        auto* const graphics_queue_fence = graphics_queue_fences_[ current_frame_ ];

        auto const graphics_fences = std::array{ graphics_queue_fence };

        CHECK_VK( ::vkWaitForFences(
            setup_.device,
            static_cast< uint32 >( graphics_fences.size( ) ),
            graphics_fences.data( ),
            VK_TRUE,
            max_possible_timeout
        ) );

        auto* const image_available_semaphore = image_available_semaphores_[ current_frame_ ];

        auto swapchain_image_index = uint32{ 0 };
        CHECK_VK( ::vkAcquireNextImageKHR(
            setup_.device,
            output_.swapchain,
            max_possible_timeout,
            image_available_semaphore,
            nullptr,
            &swapchain_image_index
        ) );
        auto* const framebuffer = output_.framebuffers[ swapchain_image_index ];

        CHECK_VK( ::vkResetFences(
            setup_.device,
            static_cast< uint32 >( graphics_fences.size( ) ),
            graphics_fences.data( )
        ) );

        auto* const command_buffer = command_buffers_[ current_frame_ ];
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
            .renderPass      = setup_.render_pass,
            .framebuffer     = framebuffer,
            .renderArea      = VkRect2D{
                 .offset = VkOffset2D{ .x = 0, .y = 0 },
                 .extent = output_.framebuffer_size,
            },
            .clearValueCount = static_cast< uint32 >( clear_values.size( ) ),
            .pClearValues    = clear_values.data( ),
        };
        ::vkCmdBeginRenderPass( command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE );
        ::vkCmdBindPipeline( command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.pipeline );

        auto const viewport = VkViewport{
            .x        = 0.0F,
            .y        = 0.0F,
            .width    = static_cast< float32 >( output_.framebuffer_size.width ),
            .height   = static_cast< float32 >( output_.framebuffer_size.height ),
            .minDepth = 0.0F,
            .maxDepth = 1.0F,
        };
        auto constexpr first_viewport = 0U;
        auto constexpr viewport_count = 1U;
        ::vkCmdSetViewport( command_buffer, first_viewport, viewport_count, &viewport );

        auto const scissors = VkRect2D{
            .offset = VkOffset2D{ .x = 0, .y = 0 },
            .extent = output_.framebuffer_size,
        };
        auto constexpr first_scissor = 0U;
        auto constexpr scissor_count = 1U;
        ::vkCmdSetScissor( command_buffer, first_scissor, scissor_count, &scissors );

        ::vkCmdPushConstants(
            command_buffer,
            pipeline_.pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof( pipeline_.model_uniforms ),
            &pipeline_.model_uniforms
        );

        ::vkCmdPushConstants(
            command_buffer,
            pipeline_.pipeline_layout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            sizeof( pipeline_.model_uniforms ),
            sizeof( pipeline_.display_uniforms ),
            &pipeline_.display_uniforms
        );

        auto constexpr vertex_count   = 3U;
        auto constexpr instance_count = 1U;
        auto constexpr first_vertex   = 0U;
        auto constexpr first_instance = 0U;
        ::vkCmdDraw( command_buffer, vertex_count, instance_count, first_vertex, first_instance );

        ::vkCmdEndRenderPass( command_buffer );

        CHECK_VK( ::vkEndCommandBuffer( command_buffer ) );

        auto* const render_finished_semaphore = render_finished_semaphores_[ current_frame_ ];
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
            .pSwapchains        = &output_.swapchain,
            .pImageIndices      = &swapchain_image_index,
            .pResults           = nullptr,
        };
        CHECK_VK( ::vkQueuePresentKHR( setup_.surface_queue, &present_info ) );

        current_frame_ = ( current_frame_ + 1U ) % max_frames_in_flight;

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
