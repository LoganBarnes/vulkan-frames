// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/utils/types.hpp"

// standard
#include <memory>

namespace ltb::net
{

class FdSocket
{
public:
    ~FdSocket( );

    auto initialize( ) -> bool;

    auto connect_and_send( std::string_view socket_path, int32 fd ) -> bool;

    auto bind_and_receive( std::string_view socket_path, int32& fd_out ) -> bool;

private:
    int32 unix_socket_fd_;
};

} // namespace ltb::net
