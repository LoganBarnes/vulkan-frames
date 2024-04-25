// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#include "ltb/net/fd_socket.hpp"

// project
#include "ltb/utils/ignore.hpp"

// external
#include <spdlog/spdlog.h>

// platform
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ltb::net
{
namespace
{

struct Data
{
    msghdr                                            msg  = { };
    std::array< iovec, 1 >                            iov  = { };
    std::array< char, CMSG_SPACE( sizeof( int32 ) ) > cmsg = { };

    char        c           = 'x';
    sockaddr_un socket_name = { };

    Data( )
    {
        iov[ 0 ].iov_base = &c;
        iov[ 0 ].iov_len  = sizeof( c );

        msg.msg_control    = cmsg.data( );
        msg.msg_controllen = sizeof( cmsg );
        msg.msg_name       = nullptr;
        msg.msg_namelen    = 0;
        msg.msg_flags      = 0;
        msg.msg_iov        = iov.data( );
        msg.msg_iovlen     = iov.size( );

        socket_name.sun_family = AF_UNIX;
    }
};

} // namespace

FdSocket::~FdSocket( )
{
    if ( -1 != unix_socket_fd_ )
    {
        utils::ignore( ::close( unix_socket_fd_ ) );
    }
}

auto FdSocket::initialize( ) -> bool
{
    if ( unix_socket_fd_ = ::socket( PF_UNIX, SOCK_DGRAM, 0 ); unix_socket_fd_ < 0 )
    {
        spdlog::error( "socket() failed: {}", std::strerror( errno ) );
        return false;
    }
    return true;
}

auto FdSocket::connect_and_send( std::string_view const socket_path, int32 const fd ) -> bool
{
    if ( socket_path.length( ) > ( sizeof( sockaddr_un::sun_path ) - 1 ) )
    {
        spdlog::error( "socket path too long" );
        return false;
    }
    if ( fd >= static_cast< int32 >( std::numeric_limits< uint8 >::max( ) ) )
    {
        spdlog::error( "fd too large" );
        return false;
    }

    auto data = Data{ };
    utils::ignore(
        std::strncpy( data.socket_name.sun_path, socket_path.data( ), socket_path.length( ) )
    );

    if ( ::connect(
             unix_socket_fd_,
             reinterpret_cast< sockaddr* >( &data.socket_name ),
             sizeof( data.socket_name )
         )
         < 0 )
    {
        spdlog::error( "connect() failed: {}", std::strerror( errno ) );
        return false;
    }
    spdlog::debug( "connect()" );

    auto* const cmptr = CMSG_FIRSTHDR( &data.msg );
    if ( nullptr == cmptr )
    {
        spdlog::error( "CMSG_FIRSTHDR() failed" );
        return false;
    }
    cmptr->cmsg_len         = CMSG_LEN( sizeof( fd ) );
    cmptr->cmsg_level       = SOL_SOCKET;
    cmptr->cmsg_type        = SCM_RIGHTS;
    *( CMSG_DATA( cmptr ) ) = static_cast< uint8 >( fd );

    if ( ::sendmsg( unix_socket_fd_, &data.msg, 0 ) < 0 )
    {
        spdlog::error( "sendmsg() failed: {}", std::strerror( errno ) );
        return false;
    }
    spdlog::debug( "sendmsg()" );

    return true;
}

auto FdSocket::bind_and_receive( std::string_view const socket_path, int32& fd_out ) -> bool
{
    if ( socket_path.length( ) > ( sizeof( sockaddr_un::sun_path ) - 1 ) )
    {
        spdlog::error( "socket path too long" );
        return false;
    }

    auto data = Data{ };
    utils::ignore(
        std::strncpy( data.socket_name.sun_path, socket_path.data( ), socket_path.length( ) )
    );

    spdlog::debug( "Binding to socket: {}", data.socket_name.sun_path );
    if ( ::bind(
             unix_socket_fd_,
             reinterpret_cast< sockaddr* >( &data.socket_name ),
             sizeof( data.socket_name )
         )
         < 0 )
    {
        spdlog::error( "bind() failed: {}", std::strerror( errno ) );
        return false;
    }

    if ( auto const bytes_received = ::recvmsg( unix_socket_fd_, &data.msg, 0 );
         bytes_received < 0 )
    {
        spdlog::error( "recvmsg() failed: {}", std::strerror( errno ) );
        return false;
    }
    spdlog::debug( "recvmsg()" );

    auto const* const cmptr = CMSG_FIRSTHDR( &data.msg );

    if ( ( nullptr == cmptr ) || ( CMSG_LEN( sizeof( int32 ) ) != cmptr->cmsg_len ) )
    {
        spdlog::error( "Invalid control message" );
        return false;
    }

    if ( SOL_SOCKET != cmptr->cmsg_level )
    {
        spdlog::error( "control level != SOL_SOCKET" );
        return false;
    }
    if ( SCM_RIGHTS != cmptr->cmsg_type )
    {
        spdlog::error( "control type != SCM_RIGHTS" );
        return false;
    }
    fd_out = static_cast< int32 >( *( CMSG_DATA( cmptr ) ) );

    return true;
}

} // namespace ltb::net
