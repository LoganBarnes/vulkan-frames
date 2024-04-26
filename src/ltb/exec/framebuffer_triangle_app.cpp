// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////

// project
#include "ltb/vlk/check.hpp"
#include "ltb/vlk/render.hpp"

// standard
#include <charconv>
#include <ranges>
#include <vector>

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
    vlk::SetupData< vlk::AppType::Windowed >      setup_              = { };
    vlk::PipelineData< vlk::Pipeline::Triangle >  triangle_pipeline_  = { };
    vlk::PipelineData< vlk::Pipeline::Composite > composite_pipeline_ = { };
    vlk::OutputData< vlk::AppType::Windowed >     windowed_output_    = { };
    vlk::OutputData< vlk::AppType::Headless >     headless_output_    = { };
    vlk::SyncData< vlk::AppType::Windowed >       sync_               = { };
};

App::~App( )
{
    vlk::destroy( setup_, sync_ );
    vlk::destroy( setup_, triangle_pipeline_ );
    vlk::destroy( setup_, windowed_output_ );
    vlk::destroy( setup_, headless_output_ );
    vlk::destroy( setup_ );
}

auto App::initialize( uint32 const physical_device_index ) -> bool
{
    CHECK_TRUE( vlk::initialize( physical_device_index, setup_ ) );
    CHECK_TRUE( vlk::initialize( setup_, triangle_pipeline_ ) );
    CHECK_TRUE( vlk::initialize( setup_, triangle_pipeline_, windowed_output_ ) );
    CHECK_TRUE( vlk::initialize(
        VkExtent3D{
            windowed_output_.framebuffer_size.width,
            windowed_output_.framebuffer_size.height,
            1
        },
        vlk::ExternalMemory::No,
        setup_,
        triangle_pipeline_,
        headless_output_
    ) );
    CHECK_TRUE( vlk::initialize( max_frames_in_flight, setup_, sync_ ) );
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

        // Render pipeline here.
        CHECK_TRUE( vlk::render( setup_, triangle_pipeline_, windowed_output_, sync_ ) );

        sync_.current_frame = ( sync_.current_frame + 1U ) % max_frames_in_flight;

        // This GLFW_KEY_ESCAPE bit shouldn't exist in a final product.
        should_exit = ( GLFW_TRUE == ::glfwWindowShouldClose( setup_.window ) )
                   || ( GLFW_PRESS == ::glfwGetKey( setup_.window, GLFW_KEY_ESCAPE ) );
    }

    CHECK_VK( ::vkDeviceWaitIdle( setup_.device ) );

    spdlog::info( "Exiting..." );
    return true;
}

} // namespace ltb

auto main( ltb::int32 const argc, char const* const argv[] ) -> ltb::int32
{
    spdlog::set_level( spdlog::level::debug );

    auto physical_device_index = ltb::uint32{ 0 };
    if ( argc > 1 )
    {
        auto const* const start = argv[ 1 ];
        auto const* const end   = argv[ 1 ] + std::strlen( argv[ 1 ] );

        if ( auto const result = std::from_chars( start, end, physical_device_index );
             std::errc( ) != result.ec )
        {
            spdlog::error( "Invalid argument: {}", argv[ 1 ] );
            return EXIT_FAILURE;
        }
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
