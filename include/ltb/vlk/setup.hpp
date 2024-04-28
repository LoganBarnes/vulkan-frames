// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/vlk/vlk.hpp"

// standard
#include <array>

namespace ltb::vlk
{

template < AppType app_type >
struct SetupData;

template <>
struct SetupData< AppType::Windowed >
{
    // Glfw windowing functionality
    int32       glfw   = GLFW_FALSE;
    GLFWwindow* window = { };

    // Common Vulkan objects
    VkInstance                          instance                        = { };
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
    VkDebugUtilsMessengerEXT            debug_messenger                 = { };
    VkSurfaceKHR                        surface                         = { };
    VkPhysicalDevice                    physical_device                 = { };
    uint32                              graphics_queue_family_index     = { };
    uint32                              surface_queue_family_index      = { };
    VkDevice                            device                          = { };
    VkQueue                             graphics_queue                  = { };
    VkQueue                             surface_queue                   = { };
    VkCommandPool                       graphics_command_pool           = { };
    VkSurfaceFormatKHR                  surface_format                  = { };
};

template <>
struct SetupData< AppType::Headless >
{
    // Console input polling
    std::array< char, 20 > input_buffer = { };

    // Common Vulkan objects
    VkInstance                          instance                        = { };
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
    VkDebugUtilsMessengerEXT            debug_messenger                 = { };
    VkPhysicalDevice                    physical_device                 = { };
    uint32                              graphics_queue_family_index     = { };
    VkDevice                            device                          = { };
    VkQueue                             graphics_queue                  = { };
    VkCommandPool                       graphics_command_pool           = { };
    VkFormat                            color_format                    = { };
};

/// \brief Initialize all the fields of a SetupData struct.
template < AppType app_type >
auto initialize( SetupData< app_type >& setup, uint32 physical_device_index ) -> bool;

/// \brief Destroy all the fields of a SetupData struct.
template < AppType app_type >
auto destroy( SetupData< app_type >& setup ) -> void;

} // namespace ltb::vlk
