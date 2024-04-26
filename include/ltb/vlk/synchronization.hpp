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

auto initialize( SetupData< AppType::Headless > const& setup, SyncData< AppType::Headless >& sync )
    -> bool;

auto initialize(
    uint32 const                          max_frames_in_flight,
    SetupData< AppType::Windowed > const& setup,
    SyncData< AppType::Windowed >&        sync
) -> bool;

template < AppType app_type >
auto destroy( SetupData< app_type > const& setup, SyncData< app_type >& sync ) -> void;

} // namespace ltb::vlk
