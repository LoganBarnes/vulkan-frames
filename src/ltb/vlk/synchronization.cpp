// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#include "ltb/vlk/synchronization.hpp"

// project
#include "ltb/vlk/check.hpp"

namespace ltb::vlk
{

auto initialize(
    VkDevice const&                device,
    VkCommandPool const&           command_pool,
    SyncData< AppType::Headless >& sync
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
    uint32 const                   max_frames_in_flight,
    VkDevice const&                device,
    VkCommandPool const&           command_pool,
    SyncData< AppType::Windowed >& sync
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

} // namespace ltb::vlk
