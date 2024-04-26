// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/utils/types.hpp"

// standard
#include <span>

namespace ltb::utils
{

auto get_physical_device_index_from_args(
    std::span< char const* > const& args,
    uint32&                         physical_device_index
) -> bool;

} // namespace ltb::utils
