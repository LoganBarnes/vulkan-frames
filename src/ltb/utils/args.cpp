// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#include "ltb/utils/args.hpp"

// external
#include <spdlog/spdlog.h>

// standard
#include <charconv>
#include <cstring>

namespace ltb::utils
{

auto get_physical_device_index_from_args(
    std::span< char const* > const& args,
    uint32&                         physical_device_index
) -> bool
{
    if ( args.size( ) > 1U )
    {
        auto const* const start = args[ 1 ];
        auto const* const end   = args[ 1 ] + std::strlen( args[ 1 ] );

        if ( auto const result = std::from_chars( start, end, physical_device_index );
             std::errc( ) != result.ec )
        {
            spdlog::error( "Invalid argument: {}", args[ 1 ] );
            return false;
        }
    }
    return true;
}

} // namespace ltb::utils
