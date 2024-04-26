// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

#include <cinttypes>

namespace ltb
{

using int8  = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

using uint8  = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

using float32 = float;
using float64 = double;

constexpr auto expected_float32_byte_count = 4;
constexpr auto expected_float64_byte_count = 8;

static_assert( expected_float32_byte_count == sizeof( float32 ) );
static_assert( expected_float64_byte_count == sizeof( float64 ) );

} // namespace ltb
