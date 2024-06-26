// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////

// project
#include "ltb/utils/args.hpp"
#include "ltb/vlk/check.hpp"
#include "ltb/vlk/render.hpp"

namespace ltb
{
namespace
{

constexpr auto max_frames_in_flight = uint32_t{ 2 };

} // namespace

class App
{
public:
    auto initialize( uint32 physical_device_index ) -> bool;
    auto destroy( ) -> void;
    auto run( ) -> bool;

private:
    // Vulkan data
    vlk::SetupData< vlk::AppType::Windowed >     setup_    = { };
    vlk::OutputData< vlk::AppType::Windowed >    output_   = { };
    vlk::PipelineData< vlk::Pipeline::Triangle > pipeline_ = { };
    vlk::SyncData< vlk::AppType::Windowed >      sync_     = { };
};

auto App::initialize( uint32 const physical_device_index ) -> bool
{
    CHECK_TRUE( vlk::initialize( setup_, physical_device_index ) );
    CHECK_TRUE( vlk::initialize( output_, setup_ ) );
    CHECK_TRUE( vlk::initialize( pipeline_, setup_, output_, max_frames_in_flight ) );
    CHECK_TRUE( vlk::initialize( sync_, setup_, max_frames_in_flight ) );

    return true;
}

auto App::destroy( ) -> void
{
    vlk::destroy( sync_, setup_ );
    vlk::destroy( pipeline_, setup_ );
    vlk::destroy( output_, setup_ );
    vlk::destroy( setup_ );
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

        pipeline_.model_uniforms.scale_rotation_translation[ 1 ]
            = M_PI_2f * angular_velocity_rps * current_duration_s;

        // Render pipeline here.
        CHECK_TRUE( vlk::render( setup_, pipeline_, output_, sync_ ) );

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
        app.destroy( );
        return EXIT_SUCCESS;
    }
    else
    {
        app.destroy( );
        return EXIT_FAILURE;
    }
}
