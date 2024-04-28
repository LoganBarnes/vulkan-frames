// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#pragma once

// project
#include "ltb/vlk/output.hpp"

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

    static constexpr auto vertex_count = 3U;
};

template <>
struct PipelineData< Pipeline::Composite >
{
    VkDescriptorPool               descriptor_pool       = { };
    VkDescriptorSetLayout          descriptor_set_layout = { };
    std::vector< VkDescriptorSet > descriptor_sets       = { };
    VkPipelineLayout               pipeline_layout       = { };
    VkPipeline                     pipeline              = { };

    static constexpr auto vertex_count = 4U;
};

/// \brief Initialize all the fields of a PipelineData struct.
template < Pipeline pipeline_type >
auto initialize(
    PipelineData< pipeline_type >& pipeline,
    VkDevice const&                device,
    VkRenderPass const&            render_pass,
    uint32                         max_frames_in_flight
) -> bool;

/// \brief A wrapper function around the main initialize function.
template < Pipeline pipeline_type, AppType setup_app_type >
auto initialize(
    PipelineData< pipeline_type >&         pipeline,
    SetupData< setup_app_type > const&     setup,
    OutputData< AppType::Windowed > const& output,
    uint32                                 max_frames_in_flight
) -> bool
{
    return initialize( pipeline, setup.device, output.render_pass, max_frames_in_flight );
}

template < Pipeline pipeline_type, AppType setup_app_type >
auto initialize(
    PipelineData< pipeline_type >&         pipeline,
    SetupData< setup_app_type > const&     setup,
    OutputData< AppType::Headless > const& output
) -> bool
{
    return initialize( pipeline, setup.device, output.render_pass, 1U );
}

/// \brief Destroy all the fields of a PipelineData struct.
template < Pipeline pipeline_type >
auto destroy( PipelineData< pipeline_type >& pipeline, VkDevice const& device ) -> void;

/// \brief A wrapper function around the main destroy function.
template < Pipeline pipeline_type, AppType app_type >
auto destroy( PipelineData< pipeline_type >& pipeline, SetupData< app_type > const& setup ) -> void
{
    return destroy( pipeline, setup.device );
}

} // namespace ltb::vlk
