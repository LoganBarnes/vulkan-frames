// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// external
#include <spdlog/spdlog.h>

// standard
#include <filesystem>
#include <fstream>
#include <vector>

namespace ltb::utils
{

template < typename T >
auto get_binary_file_contents( std::filesystem::path const& file_path, std::vector< T >& buffer )
    -> bool
{
    auto file = std::ifstream( file_path, std::ios::ate | std::ios::binary );

    if ( !file.is_open( ) )
    {
        spdlog::error( "Failed to open file '{}'", file_path.string( ) );
        return false;
    }

    auto const file_size = static_cast< size_t >( file.tellg( ) );

    auto const buffer_size = file_size / sizeof( T );
    auto const remaining   = file_size % sizeof( T );

    buffer.resize( buffer_size + std::clamp< decltype( remaining ) >( remaining, 0, 1 ) );

    file.seekg( 0 );
    file.read(
        reinterpret_cast< char* >( buffer.data( ) ),
        static_cast< std::streamsize >( file_size )
    );
    file.close( );

    return true;
}

} // namespace ltb::utils
