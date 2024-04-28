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
struct SyncData< AppType::Windowed >
{
    std::vector< VkCommandBuffer > command_buffers            = { };
    std::vector< VkSemaphore >     image_available_semaphores = { };
    std::vector< VkSemaphore >     render_finished_semaphores = { };
    std::vector< VkFence >         graphics_queue_fences      = { };
    uint32                         current_frame              = 0U;
};

template <>
struct SyncData< AppType::Headless >
{
    VkCommandBuffer command_buffer       = { };
    VkFence         graphics_queue_fence = { };
};

/// \brief Initialize all the fields of a windowed SyncData struct.
auto initialize(
    SyncData< AppType::Windowed >& sync,
    VkDevice const&                device,
    VkCommandPool const&           command_pool,
    uint32                         max_frames_in_flight
) -> bool;

/// \brief Initialize all the fields of a headless SyncData struct.
auto initialize(
    SyncData< AppType::Headless >& sync,
    VkDevice const&                device,
    VkCommandPool const&           command_pool
) -> bool;

/// \brief A wrapper function around the main initialize function.
auto initialize(
    SyncData< AppType::Windowed >&        sync,
    SetupData< AppType::Windowed > const& setup,
    uint32                                max_frames_in_flight
) -> bool;

/// \brief A wrapper function around the main initialize function.
template < AppType setup_app_type >
auto initialize( SyncData< AppType::Headless >& sync, SetupData< setup_app_type > const& setup )
    -> bool
{
    return initialize( sync, setup.device, setup.graphics_command_pool );
}

/// \brief Destroy all the fields of an SyncData struct.
template < AppType app_type >
auto destroy(
    SyncData< app_type >& sync,
    VkDevice const&       device,
    VkCommandPool const&  graphics_command_pool
) -> void;

/// \brief A wrapper function around the main destroy function.
template < AppType sync_app_type, AppType setup_app_type >
auto destroy( SyncData< sync_app_type >& sync, SetupData< setup_app_type > const& setup ) -> void
{
    return destroy( sync, setup.device, setup.graphics_command_pool );
}

} // namespace ltb::vlk
