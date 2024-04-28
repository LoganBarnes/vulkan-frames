// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////

// project
#include "ltb/utils/args.hpp"
#include "ltb/vlk/check.hpp"
#include "ltb/vlk/render.hpp"

// https://stackoverflow.com/questions/61089060/vulkan-render-to-texture
// https://github.com/SaschaWillems/Vulkan/blob/master/examples/offscreen/offscreen.cpp

namespace ltb
{
namespace
{

constexpr auto max_frames_in_flight = uint32_t{ 2 };

} // namespace

class App
{
public:
    App( )                               = default;
    App( App const& )                    = delete;
    App( App&& )                         = delete;
    auto operator=( App const& ) -> App& = delete;
    auto operator=( App&& ) -> App&      = delete;
    ~App( );

    auto initialize( uint32 physical_device_index ) -> bool;

    auto run( ) -> bool;

private:
    // Vulkan data
    vlk::SetupData< vlk::AppType::Windowed >      windowed_setup_     = { };
    vlk::OutputData< vlk::AppType::Windowed >     windowed_output_    = { };
    vlk::PipelineData< vlk::Pipeline::Composite > composite_pipeline_ = { };
    vlk::SyncData< vlk::AppType::Windowed >       windowed_sync_      = { };

    vlk::PipelineData< vlk::Pipeline::Triangle > triangle_pipeline_ = { };
    vlk::OutputData< vlk::AppType::Headless >    headless_output_   = { };
    vlk::SyncData< vlk::AppType::Headless >      headless_sync_     = { };

    VkSampler color_image_sampler_ = { };
};

App::~App( )
{
    if ( nullptr != color_image_sampler_ )
    {
        ::vkDestroySampler( windowed_setup_.device, color_image_sampler_, nullptr );
        spdlog::debug( "vkDestroySampler()" );
    }

    vlk::destroy( windowed_setup_, headless_sync_ );
    vlk::destroy( windowed_setup_, headless_output_ );
    vlk::destroy( windowed_setup_, triangle_pipeline_ );

    vlk::destroy( windowed_setup_, windowed_sync_ );
    vlk::destroy( windowed_setup_, windowed_output_ );
    vlk::destroy( windowed_setup_, composite_pipeline_ );

    vlk::destroy( windowed_setup_ );
}

auto App::initialize( uint32 const physical_device_index ) -> bool
{
    CHECK_TRUE( vlk::initialize( physical_device_index, windowed_setup_ ) );

    CHECK_TRUE( vlk::initialize< vlk::AppType::Windowed >(
        max_frames_in_flight,
        windowed_setup_.surface_format.format,
        windowed_setup_.device,
        composite_pipeline_
    ) );
    CHECK_TRUE( vlk::initialize( windowed_setup_, composite_pipeline_, windowed_output_ ) );
    CHECK_TRUE( vlk::initialize(
        max_frames_in_flight,
        windowed_setup_.device,
        windowed_setup_.graphics_command_pool,
        windowed_sync_
    ) );

    CHECK_TRUE( vlk::initialize<
                vlk::AppType::
                    Headless >( windowed_setup_.surface_format.format, windowed_setup_.device, triangle_pipeline_ ) );
    CHECK_TRUE( vlk::initialize(
        VkExtent3D{
            windowed_output_.framebuffer_size.width,
            windowed_output_.framebuffer_size.height,
            1U
        },
        vlk::ExternalMemory::Yes,
        windowed_setup_,
        triangle_pipeline_,
        headless_output_
    ) );
    CHECK_TRUE( vlk::initialize( windowed_setup_.device, windowed_setup_.graphics_command_pool, headless_sync_ ) );

    auto physical_device_properties = VkPhysicalDeviceProperties{ };
    ::vkGetPhysicalDeviceProperties( windowed_setup_.physical_device, &physical_device_properties );

    auto const sampler_info = VkSamplerCreateInfo{
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0U,
        .magFilter               = VK_FILTER_LINEAR,
        .minFilter               = VK_FILTER_LINEAR,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias              = 0.0F,
        .anisotropyEnable        = VK_TRUE,
        .maxAnisotropy           = physical_device_properties.limits.maxSamplerAnisotropy,
        .compareEnable           = VK_FALSE,
        .compareOp               = VK_COMPARE_OP_ALWAYS,
        .minLod                  = 0.0F,
        .maxLod                  = 0.0F,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };
    CHECK_VK( ::vkCreateSampler( windowed_setup_.device, &sampler_info, nullptr, &color_image_sampler_ ) );

    for ( auto i = 0U; i < max_frames_in_flight; ++i )
    {
        auto const image_info = VkDescriptorImageInfo{
            .sampler     = color_image_sampler_,
            .imageView   = headless_output_.color_image_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        auto const descriptor_writes = std::array{
            VkWriteDescriptorSet{
                .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext            = nullptr,
                .dstSet           = composite_pipeline_.descriptor_sets[ i ],
                .dstBinding       = 0U,
                .dstArrayElement  = 0U,
                .descriptorCount  = 1U,
                .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo       = &image_info,
                .pBufferInfo      = nullptr,
                .pTexelBufferView = nullptr,
            },
        };
        ::vkUpdateDescriptorSets(
            windowed_setup_.device,
            static_cast< uint32_t >( descriptor_writes.size( ) ),
            descriptor_writes.data( ),
            0U,
            nullptr
        );
    }

    return true;
}

auto App::run( ) -> bool
{
    spdlog::info( "Running render loop..." );

    auto const start_time = std::chrono::steady_clock::now( );

    using FloatSeconds = std::chrono::duration< float, std::chrono::seconds::period >;
    auto constexpr angular_velocity_rps = 0.5F;

    auto should_exit = false;
    while ( !should_exit )
    {
        ::glfwPollEvents( );

        auto const current_duration = start_time - std::chrono::steady_clock::now( );
        auto const current_duration_s
            = std::chrono::duration_cast< FloatSeconds >( current_duration ).count( );

        triangle_pipeline_.model_uniforms.scale_rotation_translation[ 1 ]
            = M_PI_2f * angular_velocity_rps * current_duration_s;

        // Render offline triangle.
        CHECK_TRUE( vlk::render( windowed_setup_, triangle_pipeline_, headless_output_, headless_sync_ ) );

        // Render pipeline here.
        CHECK_TRUE( vlk::render( windowed_setup_, composite_pipeline_, windowed_output_, windowed_sync_ ) );

        windowed_sync_.current_frame = ( windowed_sync_.current_frame + 1U ) % max_frames_in_flight;

        // This GLFW_KEY_ESCAPE bit shouldn't exist in a final product.
        should_exit = ( GLFW_TRUE == ::glfwWindowShouldClose( windowed_setup_.window ) )
                   || ( GLFW_PRESS == ::glfwGetKey( windowed_setup_.window, GLFW_KEY_ESCAPE ) );
    }

    CHECK_VK( ::vkDeviceWaitIdle( windowed_setup_.device ) );

    spdlog::info( "Exiting..." );
    return true;
}

} // namespace ltb

auto main( ltb::int32 const argc, char const* argv[] ) -> ltb::int32
{
    spdlog::set_level( spdlog::level::debug );

    auto physical_device_index = ltb::uint32{ 0 };
    if ( !ltb::utils::get_physical_device_index_from_args(
             { argv, static_cast< size_t >( argc ) },
             physical_device_index
         ) )
    {
        return EXIT_FAILURE;
    }

    if ( auto app = ltb::App( ); app.initialize( physical_device_index ) && app.run( ) )
    {
        spdlog::info( "Done." );
        return EXIT_SUCCESS;
    }
    else
    {
        return EXIT_FAILURE;
    }
}
