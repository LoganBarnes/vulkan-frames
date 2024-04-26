// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/vlk/setup.hpp"

// standard
#include <vector>

namespace ltb::vlk
{

auto constexpr uniform_alignment = 16;

struct ModelUniforms
{
    alignas( uniform_alignment ) std::array< float32, 4 > scale_rotation_translation = {
        1.0F,
        0.0F,
        0.0F,
        0.0F,
    };
};

struct DisplayUniforms
{
    alignas( uniform_alignment ) std::array< float32, 4 > color = { 1.0F, 1.0F, 1.0F, 1.0F };
};

auto constexpr float_size = sizeof( float32 );
auto constexpr vec4_size  = float_size * 4;

static_assert( vec4_size == uniform_alignment );
static_assert( sizeof( ModelUniforms ) == vec4_size );
static_assert( alignof( ModelUniforms ) == uniform_alignment );
static_assert( sizeof( DisplayUniforms ) == vec4_size );
static_assert( alignof( DisplayUniforms ) == uniform_alignment );

template < Pipeline pipeline_type >
struct PipelineData;

template <>
struct PipelineData< Pipeline::Triangle >
{
    ModelUniforms    model_uniforms   = { };
    DisplayUniforms  display_uniforms = { };
    VkPipelineLayout pipeline_layout  = { };
    VkPipeline       pipeline         = { };
};

template <>
struct PipelineData< Pipeline::Composite >
{
};

template < AppType app_type, Pipeline pipeline_type >
auto initialize( SetupData< app_type > const& setup, PipelineData< pipeline_type >& pipeline )
    -> bool;

template < AppType app_type, Pipeline pipeline_type >
auto destroy( SetupData< app_type > const& setup, PipelineData< pipeline_type >& pipeline ) -> void
{
    if ( nullptr != pipeline.pipeline )
    {
        ::vkDestroyPipeline( setup.device, pipeline.pipeline, nullptr );
        spdlog::debug( "vkDestroyPipeline()" );
    }

    if ( nullptr != pipeline.pipeline_layout )
    {
        ::vkDestroyPipelineLayout( setup.device, pipeline.pipeline_layout, nullptr );
        spdlog::debug( "vkDestroyPipelineLayout()" );
    }
}

} // namespace ltb::vlk
