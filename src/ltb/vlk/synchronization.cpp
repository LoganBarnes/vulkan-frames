// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#include "ltb/vlk/synchronization.hpp"

// project
#include "ltb/vlk/check.hpp"

namespace ltb::vlk
{

auto initialize( SetupData< AppType::Headless > const& setup, SyncData< AppType::Headless >& sync )
    -> bool
{
    auto const cmd_buf_alloc_info = VkCommandBufferAllocateInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = setup.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    CHECK_VK( ::vkAllocateCommandBuffers( setup.device, &cmd_buf_alloc_info, &sync.command_buffer )
    );
    spdlog::debug( "vkAllocateCommandBuffers()" );

    auto const fence_create_info = VkFenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    CHECK_VK(
        ::vkCreateFence( setup.device, &fence_create_info, nullptr, &sync.graphics_queue_fence )
    );
    spdlog::debug( "vkCreateFence()" );

    return true;
}

auto initialize(
    uint32 const                          max_frames_in_flight,
    SetupData< AppType::Windowed > const& setup,
    SyncData< AppType::Windowed >&        sync
) -> bool
{
    auto const cmd_buf_alloc_info = VkCommandBufferAllocateInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = setup.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = max_frames_in_flight,
    };
    sync.command_buffers.resize( cmd_buf_alloc_info.commandBufferCount );
    CHECK_VK( ::vkAllocateCommandBuffers(
        setup.device,
        &cmd_buf_alloc_info,
        sync.command_buffers.data( )
    ) );
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
            setup.device,
            &semaphore_create_info,
            nullptr,
            sync.image_available_semaphores.data( ) + i
        ) );
        CHECK_VK( ::vkCreateSemaphore(
            setup.device,
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
            setup.device,
            &fence_create_info,
            nullptr,
            sync.graphics_queue_fences.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateFence()x{}", max_frames_in_flight );

    return true;
}

template <>
auto destroy( SetupData< AppType::Headless > const& setup, SyncData< AppType::Headless >& sync )
    -> void
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

template <>
auto destroy( SetupData< AppType::Windowed > const& setup, SyncData< AppType::Windowed >& sync )
    -> void
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

} // namespace ltb::vlk
