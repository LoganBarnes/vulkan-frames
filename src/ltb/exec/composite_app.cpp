// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////

// project
#include "ltb/net/fd_socket.hpp"
#include "ltb/utils/args.hpp"
#include "ltb/utils/ignore.hpp"
#include "ltb/vlk/check.hpp"
#include "ltb/vlk/render.hpp"

// standard
#include <cerrno>

namespace ltb
{
namespace
{

auto constexpr image_extents = VkExtent3D{
    .width  = 1920,
    .height = 1080,
    .depth  = 1,
};

constexpr auto max_frames_in_flight = uint32_t{ 2 };

struct SurfaceFormatEquals
{
    VkSurfaceFormatKHR const& format;

    auto operator( )( VkSurfaceFormatKHR const& surface_format ) const -> bool
    {
        return ( surface_format.format == format.format )
            && ( surface_format.colorSpace == format.colorSpace );
    }
};

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
    vlk::SetupData< vlk::AppType::Windowed >      setup_    = { };
    vlk::PipelineData< vlk::Pipeline::Composite > pipeline_ = { };
    vlk::OutputData< vlk::AppType::Windowed >     output_   = { };
    vlk::SyncData< vlk::AppType::Windowed >       sync_     = { };

    // Imported Image
    VkImage        color_image_         = { };
    VkDeviceMemory color_image_memory_  = { };
    VkImageView    color_image_view_    = { };
    VkSampler      color_image_sampler_ = { };

    // Networking
    net::FdSocket socket_         = { };
    int32         color_image_fd_ = -1;
};

App::~App( )
{
    if ( -1 != color_image_fd_ )
    {
        utils::ignore( ::close( color_image_fd_ ) );
    }

    if ( nullptr != color_image_sampler_ )
    {
        ::vkDestroySampler( setup_.device, color_image_sampler_, nullptr );
        spdlog::debug( "vkDestroySampler()" );
    }

    if ( nullptr != color_image_view_ )
    {
        ::vkDestroyImageView( setup_.device, color_image_view_, nullptr );
        spdlog::debug( "vkDestroyImageView()" );
    }

    if ( nullptr != color_image_memory_ )
    {
        ::vkFreeMemory( setup_.device, color_image_memory_, nullptr );
        spdlog::debug( "vkFreeMemory()" );
    }

    if ( nullptr != color_image_ )
    {
        ::vkDestroyImage( setup_.device, color_image_, nullptr );
        spdlog::debug( "vkDestroyImage()" );
    }

    vlk::destroy( setup_, sync_ );
    vlk::destroy( setup_, output_ );
    vlk::destroy( setup_, pipeline_ );
    vlk::destroy( setup_ );
}

auto App::initialize( uint32 const physical_device_index ) -> bool
{
    CHECK_TRUE( vlk::initialize( physical_device_index, setup_ ) );
    CHECK_TRUE( vlk::initialize< vlk::AppType::Windowed >(
        max_frames_in_flight,
        setup_.surface_format.format,
        setup_.device,
        pipeline_
    ) );
    CHECK_TRUE( vlk::initialize( setup_, pipeline_, output_ ) );
    CHECK_TRUE(
        vlk::initialize( max_frames_in_flight, setup_.device, setup_.graphics_command_pool, sync_ )
    );

    auto constexpr socket_path = "socket";

    if ( ::unlink( socket_path ) < 0 )
    {
        if ( errno != ENOENT )
        {
            spdlog::error( "unlink() failed: {}", std::strerror( errno ) );
            return false;
        }
    }

    if ( !socket_.initialize( ) )
    {
        return false;
    }
    if ( !socket_.bind_and_receive( socket_path, color_image_fd_ ) )
    {
        return false;
    }

    spdlog::debug( "Received color image FD: {}", color_image_fd_ );

    auto constexpr external_memory_handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    auto constexpr color_format                = VK_FORMAT_B8G8R8A8_SRGB;

    auto const external_color_image_info = VkExternalMemoryImageCreateInfo{
        .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext       = nullptr,
        .handleTypes = external_memory_handle_type,
    };
    auto const color_image_create_info = VkImageCreateInfo{
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext                 = &external_color_image_info,
        .flags                 = 0U,
        .imageType             = VK_IMAGE_TYPE_2D,
        .format                = color_format,
        .extent                = image_extents,
        .mipLevels             = 1U,
        .arrayLayers           = 1U,
        .samples               = VK_SAMPLE_COUNT_1_BIT,
        .tiling                = VK_IMAGE_TILING_OPTIMAL,
        .usage                 = VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0U,
        .pQueueFamilyIndices   = nullptr,
        .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    CHECK_VK( ::vkCreateImage( setup_.device, &color_image_create_info, nullptr, &color_image_ ) );
    spdlog::debug( "vkCreateImage()" );

    auto color_image_mem_reqs = VkMemoryRequirements{ };
    ::vkGetImageMemoryRequirements( setup_.device, color_image_, &color_image_mem_reqs );

    auto memory_props = VkPhysicalDeviceMemoryProperties{ };
    ::vkGetPhysicalDeviceMemoryProperties( setup_.physical_device, &memory_props );
    auto const mem_prop_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    auto memory_type_index = std::optional< uint32 >{ };
    for ( auto i = 0U; i < memory_props.memoryTypeCount; ++i )
    {
        auto const type_is_suitable
            = ( 0U != ( color_image_mem_reqs.memoryTypeBits & ( 1U << i ) ) );
        auto const props_exist
            = ( memory_props.memoryTypes[ i ].propertyFlags & mem_prop_flags ) == mem_prop_flags;

        if ( ( !memory_type_index ) && type_is_suitable && props_exist )
        {
            memory_type_index = uint32{ i };
        }
    }

    if ( !memory_type_index )
    {
        spdlog::error( "No suitable memory type found" );
        return false;
    }

    auto const import_memory_info = VkImportMemoryFdInfoKHR{
        .sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        .pNext      = nullptr,
        .handleType = external_memory_handle_type,
        .fd         = color_image_fd_
    };
    auto const color_image_alloc_info = VkMemoryAllocateInfo{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = &import_memory_info,
        .allocationSize  = color_image_mem_reqs.size,
        .memoryTypeIndex = memory_type_index.value( ),
    };
    CHECK_VK(
        ::vkAllocateMemory( setup_.device, &color_image_alloc_info, nullptr, &color_image_memory_ )
    );
    spdlog::debug( "vkAllocateMemory()" );

    auto constexpr memory_offset = VkDeviceSize{ 0 };
    CHECK_VK( ::vkBindImageMemory( setup_.device, color_image_, color_image_memory_, memory_offset )
    );
    spdlog::debug( "vkBindImageMemory()" );

    auto const command_buffer_alloc_info = VkCommandBufferAllocateInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = setup_.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1U,
    };

    auto* command_buffer = VkCommandBuffer{ };
    CHECK_VK(
        ::vkAllocateCommandBuffers( setup_.device, &command_buffer_alloc_info, &command_buffer )
    );

    auto const begin_info = VkCommandBufferBeginInfo{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    CHECK_VK( ::vkBeginCommandBuffer( command_buffer, &begin_info ) );

    auto const barrier = VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_NONE,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = color_image_,
        .subresourceRange = VkImageSubresourceRange{
               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel   = 0U,
               .levelCount     = 1U,
               .baseArrayLayer = 0U,
               .layerCount     = 1U,
        },
    };
    ::vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0U,
        0U,
        nullptr,
        0U,
        nullptr,
        1U,
        &barrier
    );

    CHECK_VK( ::vkEndCommandBuffer( command_buffer ) );

    auto const submit_info = VkSubmitInfo{
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext                = nullptr,
        .waitSemaphoreCount   = 0,
        .pWaitSemaphores      = nullptr,
        .pWaitDstStageMask    = nullptr,
        .commandBufferCount   = 1U,
        .pCommandBuffers      = &command_buffer,
        .signalSemaphoreCount = 0U,
        .pSignalSemaphores    = nullptr,
    };
    CHECK_VK( ::vkQueueSubmit( setup_.graphics_queue, 1, &submit_info, nullptr ) );
    CHECK_VK( ::vkQueueWaitIdle( setup_.graphics_queue ) );

    ::vkFreeCommandBuffers( setup_.device, setup_.graphics_command_pool, 1U, &command_buffer );

    auto const color_image_view_create_info = VkImageViewCreateInfo{
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .image            = color_image_,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = color_format,
        .components       = VkComponentMapping{ },
        .subresourceRange = VkImageSubresourceRange{
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    CHECK_VK( ::vkCreateImageView(
        setup_.device,
        &color_image_view_create_info,
        nullptr,
        &color_image_view_
    ) );
    spdlog::debug( "vkCreateImageView()" );

    auto physical_device_properties = VkPhysicalDeviceProperties{ };
    ::vkGetPhysicalDeviceProperties( setup_.physical_device, &physical_device_properties );

    auto const sampler_info = VkSamplerCreateInfo{
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0U,
        .magFilter               = VK_FILTER_LINEAR,
        .minFilter               = VK_FILTER_LINEAR,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
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
    CHECK_VK( ::vkCreateSampler( setup_.device, &sampler_info, nullptr, &color_image_sampler_ ) );

    for ( auto i = 0U; i < max_frames_in_flight; ++i )
    {
        auto const image_info = VkDescriptorImageInfo{
            .sampler     = color_image_sampler_,
            .imageView   = color_image_view_,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        auto const descriptor_writes = std::array{
            VkWriteDescriptorSet{
                .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext            = nullptr,
                .dstSet           = pipeline_.descriptor_sets[ i ],
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
            setup_.device,
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

    auto should_exit = false;
    while ( !should_exit )
    {
        ::glfwPollEvents( );

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
        return EXIT_SUCCESS;
    }
    else
    {
        return EXIT_FAILURE;
    }
}
