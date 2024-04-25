// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

#define CHECK_VK( call )                                                                           \
    do                                                                                             \
    {                                                                                              \
        if ( auto const result = ( call ); VK_SUCCESS != result )                                  \
        {                                                                                          \
            spdlog::error( "{} failed: {}", #call, std::to_string( result ) );                     \
            return false;                                                                          \
        }                                                                                          \
    }                                                                                              \
    while ( false )

#define CHECK_TRUE( call )                                                                         \
    do                                                                                             \
    {                                                                                              \
        if ( !( call ) )                                                                           \
        {                                                                                          \
            spdlog::error( "{} failed", #call );                                                   \
            return false;                                                                          \
        }                                                                                          \
    }                                                                                              \
    while ( false )
