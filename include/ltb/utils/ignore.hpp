// A Logan Thomas Barnes project
#pragma once

namespace ltb::utils
{

template < typename... T >
auto ignore( T&&... ) -> void
{
    // This method exists to make ignored values explicit.
    // Nothing needs to happen here.
}

} // namespace ltb::utils
