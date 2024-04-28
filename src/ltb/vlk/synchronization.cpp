// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#include "ltb/vlk/synchronization.hpp"

// project
#include "ltb/vlk/check.hpp"

namespace ltb::vlk
{

auto initialize(
    SyncData< AppType::Windowed >& sync,
    VkDevice const&                device,
    VkCommandPool const&           command_pool,
    uint32 const                   max_frames_in_flight
) -> bool
{
    auto const cmd_buf_alloc_info = VkCommandBufferAllocateInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = max_frames_in_flight,
    };
    sync.command_buffers.resize( cmd_buf_alloc_info.commandBufferCount );
    CHECK_VK(
        ::vkAllocateCommandBuffers( device, &cmd_buf_alloc_info, sync.command_buffers.data( ) )
    );
    spdlog::debug( "vkAllocateCommandBuffers()" );

    sync.image_available_semaphores.resize( max_frames_in_flight );
    sync.render_finished_semaphores.resize( max_frames_in_flight );

    auto const semaphore_create_info = VkSemaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
    };
    for ( auto i = 0U; i < max_frames_in_flight; ++i )
    {
        CHECK_VK( ::vkCreateSemaphore(
            device,
            &semaphore_create_info,
            nullptr,
            sync.image_available_semaphores.data( ) + i
        ) );
        CHECK_VK( ::vkCreateSemaphore(
            device,
            &semaphore_create_info,
            nullptr,
            sync.render_finished_semaphores.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateSemaphore()x{}", max_frames_in_flight );
    spdlog::debug( "vkCreateSemaphore()x{}", max_frames_in_flight );

    sync.graphics_queue_fences.resize( max_frames_in_flight );

    auto const fence_create_info = VkFenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for ( auto i = 0U; i < max_frames_in_flight; ++i )
    {
        CHECK_VK( ::vkCreateFence(
            device,
            &fence_create_info,
            nullptr,
            sync.graphics_queue_fences.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateFence()x{}", max_frames_in_flight );

    return true;
}

auto initialize(
    SyncData< AppType::Headless >& sync,
    VkDevice const&                device,
    VkCommandPool const&           command_pool
) -> bool
{
    auto const cmd_buf_alloc_info = VkCommandBufferAllocateInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    CHECK_VK( ::vkAllocateCommandBuffers( device, &cmd_buf_alloc_info, &sync.command_buffer ) );
    spdlog::debug( "vkAllocateCommandBuffers()" );

    auto const fence_create_info = VkFenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    CHECK_VK( ::vkCreateFence( device, &fence_create_info, nullptr, &sync.graphics_queue_fence ) );
    spdlog::debug( "vkCreateFence()" );

    return true;
}

auto initialize(
    SyncData< AppType::Windowed >&        sync,
    SetupData< AppType::Windowed > const& setup,
    uint32                const                max_frames_in_flight
) -> bool
{
    return initialize( sync, setup.device, setup.graphics_command_pool, max_frames_in_flight );
}

template < AppType app_type >
auto destroy(
    SyncData< app_type >& sync,
    VkDevice const&       device,
    VkCommandPool const&  graphics_command_pool
) -> void
{
    if constexpr ( AppType::Headless == app_type )
    {
        if ( nullptr != sync.graphics_queue_fence )
        {
            ::vkDestroyFence( device, sync.graphics_queue_fence, nullptr );
            spdlog::debug( "vkDestroyFence()" );
        }

        if ( nullptr != sync.command_buffer )
        {
            ::vkFreeCommandBuffers( device, graphics_command_pool, 1, &sync.command_buffer );
            spdlog::debug( "vkFreeCommandBuffers()" );
        }
    }
    else
    {
        for ( auto* const fence : sync.graphics_queue_fences )
        {
            ::vkDestroyFence( device, fence, nullptr );
        }
        spdlog::debug( "vkDestroyFence()x{}", sync.graphics_queue_fences.size( ) );
        sync.graphics_queue_fences.clear( );

        for ( auto* const semaphore : sync.render_finished_semaphores )
        {
            ::vkDestroySemaphore( device, semaphore, nullptr );
        }
        spdlog::debug( "vkDestroySemaphore()x{}", sync.render_finished_semaphores.size( ) );
        sync.render_finished_semaphores.clear( );

        for ( auto* const semaphore : sync.image_available_semaphores )
        {
            ::vkDestroySemaphore( device, semaphore, nullptr );
        }
        spdlog::debug( "vkDestroySemaphore()x{}", sync.image_available_semaphores.size( ) );
        sync.image_available_semaphores.clear( );

        if ( !sync.command_buffers.empty( ) )
        {
            ::vkFreeCommandBuffers(
                device,
                graphics_command_pool,
                static_cast< uint32 >( sync.command_buffers.size( ) ),
                sync.command_buffers.data( )
            );
            spdlog::debug( "vkFreeCommandBuffers()" );
        }
    }
}

template auto
destroy( SyncData< AppType::Windowed >&, VkDevice const&, VkCommandPool const& ) -> void;

template auto
destroy( SyncData< AppType::Headless >&, VkDevice const&, VkCommandPool const& ) -> void;

} // namespace ltb::vlk
