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
    Windowed,
    Headless
};

enum class ExternalMemory
{
    None,
    Import,
    Export,
};

enum class Pipeline
{
    Triangle,
    Composite,
};

} // namespace ltb::vlk
