// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/vlk/output.hpp"
#include "ltb/vlk/synchronization.hpp"

// standard
#include <vector>

namespace ltb::vlk
{

constexpr auto max_possible_timeout = std::numeric_limits< uint64_t >::max( );

template < AppType setup_app_type, Pipeline pipeline_type, AppType output_app_type >
auto render(
    SetupData< setup_app_type > const&   setup,
    PipelineData< pipeline_type > const& pipeline,
    OutputData< output_app_type > const& output,
    SyncData< output_app_type > const&   sync
) -> bool
{
    auto* graphics_queue_fence = VkFence{ };

    if constexpr ( AppType::Windowed == output_app_type )
    {
        graphics_queue_fence = sync.graphics_queue_fences[ sync.current_frame ];
    }
    else
    {
        graphics_queue_fence = sync.graphics_queue_fence;
    }

    auto const graphics_fences = std::array{ graphics_queue_fence };

    CHECK_VK( ::vkWaitForFences(
        setup.device,
        static_cast< uint32 >( graphics_fences.size( ) ),
        graphics_fences.data( ),
        VK_TRUE,
        max_possible_timeout
    ) );

    auto  swapchain_image_index = uint32{ 0 };
    auto* framebuffer           = VkFramebuffer{ };
    auto* command_buffer        = VkCommandBuffer{ };

    if constexpr ( AppType::Windowed == output_app_type )
    {
        CHECK_VK( ::vkAcquireNextImageKHR(
            setup.device,
            output.swapchain,
            max_possible_timeout,
            sync.image_available_semaphores[ sync.current_frame ],
            nullptr,
            &swapchain_image_index
        ) );
        framebuffer    = output.framebuffers[ swapchain_image_index ];
        command_buffer = sync.command_buffers[ sync.current_frame ];
    }
    else
    {
        framebuffer    = output.framebuffer;
        command_buffer = sync.command_buffer;
    }

    CHECK_VK( ::vkResetFences(
        setup.device,
        static_cast< uint32 >( graphics_fences.size( ) ),
        graphics_fences.data( )
    ) );

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
        .renderPass      = pipeline.render_pass,
        .framebuffer     = framebuffer,
        .renderArea      = VkRect2D{
             .offset = VkOffset2D{ .x = 0, .y = 0 },
             .extent = output.framebuffer_size,
        },
        .clearValueCount = static_cast< uint32 >( clear_values.size( ) ),
        .pClearValues    = clear_values.data( ),
    };
    ::vkCmdBeginRenderPass( command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE );
    ::vkCmdBindPipeline( command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline );

    auto const viewport = VkViewport{
        .x        = 0.0F,
        .y        = 0.0F,
        .width    = static_cast< float32 >( output.framebuffer_size.width ),
        .height   = static_cast< float32 >( output.framebuffer_size.height ),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    };
    auto constexpr first_viewport = 0U;
    auto constexpr viewport_count = 1U;
    ::vkCmdSetViewport( command_buffer, first_viewport, viewport_count, &viewport );

    auto const scissors = VkRect2D{
        .offset = VkOffset2D{ .x = 0, .y = 0 },
        .extent = output.framebuffer_size,
    };
    auto constexpr first_scissor = 0U;
    auto constexpr scissor_count = 1U;
    ::vkCmdSetScissor( command_buffer, first_scissor, scissor_count, &scissors );

    if constexpr ( Pipeline::Triangle == pipeline_type )
    {
        ::vkCmdPushConstants(
            command_buffer,
            pipeline.pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof( pipeline.model_uniforms ),
            &pipeline.model_uniforms
        );

        ::vkCmdPushConstants(
            command_buffer,
            pipeline.pipeline_layout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            sizeof( pipeline.model_uniforms ),
            sizeof( pipeline.display_uniforms ),
            &pipeline.display_uniforms
        );
    }
    else
    {
        auto constexpr first_set            = 0U;
        auto constexpr descriptor_set_count = 1U;
        auto constexpr dynamic_offset_count = 0U;
        auto constexpr dynamic_offsets      = nullptr;
        ::vkCmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline.pipeline_layout,
            first_set,
            descriptor_set_count,
            &pipeline.descriptor_sets[ sync.current_frame ],
            dynamic_offset_count,
            dynamic_offsets
        );
    }

    auto constexpr vertex_count   = PipelineData< pipeline_type >::vertex_count;
    auto constexpr instance_count = 1U;
    auto constexpr first_vertex   = 0U;
    auto constexpr first_instance = 0U;
    ::vkCmdDraw( command_buffer, vertex_count, instance_count, first_vertex, first_instance );

    ::vkCmdEndRenderPass( command_buffer );

    CHECK_VK( ::vkEndCommandBuffer( command_buffer ) );

    if constexpr ( AppType::Windowed == output_app_type )
    {
        auto constexpr semaphore_stage
            = VkPipelineStageFlags{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        auto const submit_info = VkSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            // Color attachment stage must wait for the image acquisition to finish.
            .waitSemaphoreCount = 1U,
            .pWaitSemaphores    = &sync.image_available_semaphores[ sync.current_frame ],
            .pWaitDstStageMask  = &semaphore_stage,
            // The commands being submitted to the graphics device.
            .commandBufferCount = 1U,
            .pCommandBuffers    = &command_buffer,
            // Signal the render finished semaphore so future
            // commands waiting on this step can proceed.
            .signalSemaphoreCount = 1U,
            .pSignalSemaphores    = &sync.render_finished_semaphores[ sync.current_frame ],
        };

        // Use the fence to block future CPU code that also references this fence.
        auto constexpr submit_count = 1;
        CHECK_VK( ::vkQueueSubmit(
            setup.graphics_queue,
            submit_count,
            &submit_info,
            graphics_queue_fence
        ) );

        auto const present_info = VkPresentInfoKHR{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            // Wait for the render to finish before presenting.
            .waitSemaphoreCount = 1U,
            .pWaitSemaphores    = &sync.render_finished_semaphores[ sync.current_frame ],
            .swapchainCount     = 1U,
            .pSwapchains        = &output.swapchain,
            .pImageIndices      = &swapchain_image_index,
            .pResults           = nullptr,
        };
        CHECK_VK( ::vkQueuePresentKHR( setup.surface_queue, &present_info ) );
    }
    else
    {
        auto const submit_info = VkSubmitInfo{
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = nullptr,
            .waitSemaphoreCount   = 0,
            .pWaitSemaphores      = nullptr,
            .pWaitDstStageMask    = nullptr,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &command_buffer,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores    = nullptr,
        };
        auto constexpr submit_count = 1;
        CHECK_VK( ::vkQueueSubmit(
            setup.graphics_queue,
            submit_count,
            &submit_info,
            graphics_queue_fence
        ) );
    }

    return true;
}

} // namespace ltb::vlk
