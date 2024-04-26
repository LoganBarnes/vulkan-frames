// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/utils/types.hpp"

// external
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

namespace ltb::vlk
{

enum class AppType
{
    Headless,
    Windowed
};

enum class ExternalMemory
{
    No,
    Yes
};

enum class Pipeline
{
    Triangle,
    Composite,
};

} // namespace ltb::vlk
