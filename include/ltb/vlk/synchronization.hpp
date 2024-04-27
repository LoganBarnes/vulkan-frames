// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/vlk/setup.hpp"

// standard
#include <vector>

namespace ltb::vlk
{

template < AppType app_type >
struct SyncData;

template <>
struct SyncData< AppType::Headless >
{
    VkCommandBuffer command_buffer       = { };
    VkFence         graphics_queue_fence = { };
};

template <>
struct SyncData< AppType::Windowed >
{
    std::vector< VkCommandBuffer > command_buffers            = { };
    std::vector< VkSemaphore >     image_available_semaphores = { };
    std::vector< VkSemaphore >     render_finished_semaphores = { };
    std::vector< VkFence >         graphics_queue_fences      = { };
    uint32                         current_frame              = 0U;
};

auto initialize(
    VkDevice const&                device,
    VkCommandPool const&           command_pool,
    SyncData< AppType::Headless >& sync
) -> bool;

auto initialize(
    uint32                         max_frames_in_flight,
    VkDevice const&                device,
    VkCommandPool const&           command_pool,
    SyncData< AppType::Windowed >& sync
) -> bool;

template < AppType setup_app_type, AppType app_type >
auto destroy( SetupData< setup_app_type > const& setup, SyncData< app_type >& sync ) -> void
{
    if constexpr ( AppType::Headless == app_type )
    {
        if ( nullptr != sync.graphics_queue_fence )
        {
            ::vkDestroyFence( setup.device, sync.graphics_queue_fence, nullptr );
            spdlog::debug( "vkDestroyFence()" );
        }

        if ( nullptr != sync.command_buffer )
        {
            ::vkFreeCommandBuffers(
                setup.device,
                setup.graphics_command_pool,
                1,
                &sync.command_buffer
            );
            spdlog::debug( "vkFreeCommandBuffers()" );
        }
    }
    else
    {
        for ( auto* const fence : sync.graphics_queue_fences )
        {
            ::vkDestroyFence( setup.device, fence, nullptr );
        }
        spdlog::debug( "vkDestroyFence()x{}", sync.graphics_queue_fences.size( ) );
        sync.graphics_queue_fences.clear( );

        for ( auto* const semaphore : sync.render_finished_semaphores )
        {
            ::vkDestroySemaphore( setup.device, semaphore, nullptr );
        }
        spdlog::debug( "vkDestroySemaphore()x{}", sync.render_finished_semaphores.size( ) );
        sync.render_finished_semaphores.clear( );

        for ( auto* const semaphore : sync.image_available_semaphores )
        {
            ::vkDestroySemaphore( setup.device, semaphore, nullptr );
        }
        spdlog::debug( "vkDestroySemaphore()x{}", sync.image_available_semaphores.size( ) );
        sync.image_available_semaphores.clear( );

        if ( !sync.command_buffers.empty( ) )
        {
            ::vkFreeCommandBuffers(
                setup.device,
                setup.graphics_command_pool,
                static_cast< uint32 >( sync.command_buffers.size( ) ),
                sync.command_buffers.data( )
            );
            spdlog::debug( "vkFreeCommandBuffers()" );
        }
    }
}

} // namespace ltb::vlk
