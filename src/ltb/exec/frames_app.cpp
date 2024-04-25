// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////

// project
#include "ltb/net/fd_socket.hpp"
#include "ltb/utils/read_file.hpp"
#include "ltb/ltb_config.hpp"
#include "ltb/utils/ignore.hpp"

// external
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>

// standard
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <set>
#include <vector>

// platform
#include <fcntl.h>

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

namespace ltb
{
namespace
{

auto constexpr image_extents = VkExtent3D{
    .width  = 1920,
    .height = 1080,
    .depth  = 1,
};

auto constexpr external_memory_handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
auto constexpr max_possible_timeout        = std::numeric_limits< uint64_t >::max( );

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

VKAPI_ATTR VkBool32 default_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT const      message_severity,
    VkDebugUtilsMessageTypeFlagsEXT                   message_type,
    VkDebugUtilsMessengerCallbackDataEXT const* const callback_data,
    void*                                             user_data
)
{
    utils::ignore( message_type, user_data );

    switch ( message_severity )
    {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            spdlog::debug( "Validation layer: {}", callback_data->pMessage );
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            spdlog::info( "Validation layer: {}", callback_data->pMessage );
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            spdlog::warn( "Validation layer: {}", callback_data->pMessage );
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            spdlog::error( "Validation layer: {}", callback_data->pMessage );
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
            break;
    }

    return false;
}

struct DeviceQueueCreateInfo
{
    std::vector< float32 >& queue_priorities;

    auto operator( )( uint32 const queue_index ) const
    {
        auto create_info             = VkDeviceQueueCreateInfo{ };
        create_info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        create_info.pNext            = nullptr;
        create_info.flags            = 0;
        create_info.queueFamilyIndex = queue_index;
        create_info.queueCount       = static_cast< uint32 >( queue_priorities.size( ) );
        create_info.pQueuePriorities = queue_priorities.data( );
        return create_info;
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
    // Console input polling
    std::array< char, 20 > input_buffer_ = { };

    // Initialization
    VkInstance                          instance_                       = { };
    PFN_vkCreateDebugUtilsMessengerEXT  vkCreateDebugUtilsMessengerEXT  = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
    VkDebugUtilsMessengerEXT            debug_messenger_                = { };
    VkPhysicalDevice                    physical_device_                = { };
    VkDevice                            device_                         = { };
    VkQueue                             graphics_queue_                 = { };
    VkRenderPass                        render_pass_                    = { };

    // Pools
    VkCommandPool    command_pool_    = { };

    // Synchronization objects
    VkCommandBuffer command_buffer_       = { };
    VkFence         graphics_queue_fence_ = { };

    // Size dependent objects
    VkImage        color_image_        = { };
    VkDeviceMemory color_image_memory_ = { };
    VkImageView    color_image_view_   = { };
    VkFramebuffer  framebuffer_        = { };

    // Pipeline
    ModelUniforms    model_uniforms_   = { };
    DisplayUniforms  display_uniforms_ = { };
    VkPipelineLayout pipeline_layout_  = { };
    VkPipeline       pipeline_         = { };

    // Networking
    net::FdSocket socket_         = { };
    int32         color_image_fd_ = -1;
};

App::~App( )
{
    if ( ( -1 != color_image_fd_ ) && ( ::close( color_image_fd_ ) < 0 ) )
    {
        spdlog::error( "close(color_image_fd) failed: {}", std::strerror( errno ) );
    }

    if ( nullptr != pipeline_ )
    {
        ::vkDestroyPipeline( device_, pipeline_, nullptr );
        spdlog::debug( "vkDestroyPipeline()" );
    }

    if ( nullptr != pipeline_layout_ )
    {
        ::vkDestroyPipelineLayout( device_, pipeline_layout_, nullptr );
        spdlog::debug( "vkDestroyPipelineLayout()" );
    }

    if ( nullptr != framebuffer_ )
    {
        ::vkDestroyFramebuffer( device_, framebuffer_, nullptr );
        spdlog::debug( "vkDestroyFramebuffer()" );
    }

    if ( nullptr != color_image_view_ )
    {
        ::vkDestroyImageView( device_, color_image_view_, nullptr );
        spdlog::debug( "vkDestroyImageView()" );
    }

    if ( nullptr != color_image_memory_ )
    {
        ::vkFreeMemory( device_, color_image_memory_, nullptr );
        spdlog::debug( "vkFreeMemory()" );
    }

    if ( nullptr != color_image_ )
    {
        ::vkDestroyImage( device_, color_image_, nullptr );
        spdlog::debug( "vkDestroyImage()" );
    }

    if ( nullptr != graphics_queue_fence_ )
    {
        ::vkDestroyFence( device_, graphics_queue_fence_, nullptr );
        spdlog::debug( "vkDestroyFence()" );
    }

    if ( nullptr != command_buffer_ )
    {
        ::vkFreeCommandBuffers( device_, command_pool_, 1, &command_buffer_ );
        spdlog::debug( "vkFreeCommandBuffers()" );
    }

    if ( nullptr != command_pool_ )
    {
        ::vkDestroyCommandPool( device_, command_pool_, nullptr );
        spdlog::debug( "vkDestroyCommandPool()" );
    }

    if ( nullptr != render_pass_ )
    {
        ::vkDestroyRenderPass( device_, render_pass_, nullptr );
        spdlog::debug( "vkDestroyRenderPass()" );
    }

    if ( nullptr != device_ )
    {
        ::vkDestroyDevice( device_, nullptr );
        spdlog::debug( "vkDestroyDevice()" );
    }

    if ( nullptr != debug_messenger_ )
    {
        vkDestroyDebugUtilsMessengerEXT( instance_, debug_messenger_, nullptr );
        spdlog::debug( "vkDestroyDebugUtilsMessengerEXT()" );
    }

    if ( nullptr != instance_ )
    {
        ::vkDestroyInstance( instance_, nullptr );
        spdlog::debug( "vkDestroyInstance()" );
    }
}

auto App::initialize( uint32 physical_device_index ) -> bool
{
    // Setup non-blocking console input
    if ( auto const fcntl_get_result = ::fcntl( STDIN_FILENO, F_SETFL, O_NONBLOCK );
         fcntl_get_result < 0 )
    {
        spdlog::error( "fcntl() failed: {}", std::strerror( errno ) );
        return false;
    }
    else
    {
        if ( auto const fcntl_set_result
             = ::fcntl( STDIN_FILENO, F_SETFL, fcntl_get_result | O_NONBLOCK );
             fcntl_set_result < 0 )
        {
            spdlog::error( "fcntl() failed: {}", std::strerror( errno ) );
            return false;
        }
    }

    auto const extension_names = std::vector{
#if defined( __APPLE__ )
        "VK_KHR_portability_enumeration",
#endif
#if !defined( NDEBUG )
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
    };

    auto constexpr layer_names = std::array{
#if !defined( LTB_DRIVEOS_DEVICE ) && !defined( NDEBUG ) && !defined( WIN32 )
        "VK_LAYER_KHRONOS_validation",
#endif
    };

    auto constexpr flags = VkInstanceCreateFlags{
#if defined( __APPLE__ )
        VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#endif
    };

    auto const application_info = VkApplicationInfo{
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = "Vulkan app",
        .applicationVersion = VK_MAKE_API_VERSION( 0, 1, 0, 0 ),
        .pEngineName        = "No Engine",
        .engineVersion      = VK_MAKE_API_VERSION( 0, 1, 0, 0 ),
        .apiVersion         = VK_API_VERSION_1_3,
    };

    auto const debug_create_info = VkDebugUtilsMessengerCreateInfoEXT{
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext           = nullptr,
        .flags           = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
#if !defined( LTB_DRIVEOS_DEVICE ) && !defined( NDEBUG ) && !defined( WIN32 )
        .pfnUserCallback = default_debug_callback,
#else
        .pfnUserCallback = nullptr,
#endif
        .pUserData = nullptr,
    };

    auto const create_info = VkInstanceCreateInfo{
        .sType               = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext               = ( debug_create_info.pfnUserCallback ? &debug_create_info : nullptr ),
        .flags               = flags,
        .pApplicationInfo    = &application_info,
        .enabledLayerCount   = static_cast< uint32 >( layer_names.size( ) ),
        .ppEnabledLayerNames = layer_names.data( ),
        .enabledExtensionCount   = static_cast< uint32 >( extension_names.size( ) ),
        .ppEnabledExtensionNames = extension_names.data( ),
    };

    CHECK_VK( ::vkCreateInstance( &create_info, nullptr, &instance_ ) );
    spdlog::debug( "vkCreateInstance()" );

    if ( nullptr != debug_create_info.pfnUserCallback )
    {
        vkCreateDebugUtilsMessengerEXT = reinterpret_cast< PFN_vkCreateDebugUtilsMessengerEXT >(
            ::vkGetInstanceProcAddr( instance_, "vkCreateDebugUtilsMessengerEXT" )
        );

        vkDestroyDebugUtilsMessengerEXT = reinterpret_cast< PFN_vkDestroyDebugUtilsMessengerEXT >(
            ::vkGetInstanceProcAddr( instance_, "vkDestroyDebugUtilsMessengerEXT" )
        );

        if ( ( nullptr == vkCreateDebugUtilsMessengerEXT )
             || ( nullptr == vkDestroyDebugUtilsMessengerEXT ) )
        {
            spdlog::error( "vkGetInstanceProcAddr() failed" );
            return false;
        }

        CHECK_VK( vkCreateDebugUtilsMessengerEXT(
            instance_,
            &debug_create_info,
            nullptr,
            &debug_messenger_
        ) );
        spdlog::debug( "vkCreateDebugUtilsMessengerEXT()" );
    }

    auto physical_device_count = uint32{ 0 };
    CHECK_VK( ::vkEnumeratePhysicalDevices( instance_, &physical_device_count, nullptr ) );
    auto physical_devices = std::vector< VkPhysicalDevice >( physical_device_count );
    CHECK_VK(
        ::vkEnumeratePhysicalDevices( instance_, &physical_device_count, physical_devices.data( ) )
    );

    for ( auto i = uint32{ 0 }; i < physical_device_count; ++i )
    {
        auto properties = VkPhysicalDeviceProperties{ };
        ::vkGetPhysicalDeviceProperties( physical_devices[ i ], &properties );
        spdlog::info( "Device[{}]: {}", i, properties.deviceName );
    }

    if ( physical_device_index >= physical_device_count )
    {
        spdlog::error( "Invalid physical device index: {}", physical_device_index );
        return false;
    }

    physical_device_ = physical_devices[ physical_device_index ];

    auto physical_device_properties = VkPhysicalDeviceProperties{ };
    ::vkGetPhysicalDeviceProperties( physical_device_, &physical_device_properties );
    spdlog::info(
        "Using Device[{}]: {}",
        physical_device_index,
        physical_device_properties.deviceName
    );

    auto queue_family_count = uint32{ 0 };
    ::vkGetPhysicalDeviceQueueFamilyProperties( physical_device_, &queue_family_count, nullptr );
    auto queue_families = std::vector< VkQueueFamilyProperties >( queue_family_count );
    ::vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device_,
        &queue_family_count,
        queue_families.data( )
    );

    auto graphics_queue_family_index = std::optional< uint32 >{ };

    for ( auto i = uint32{ 0 }; i < queue_family_count; ++i )
    {
        if ( ( !graphics_queue_family_index )
             && ( 0 != ( queue_families[ i ].queueFlags & VK_QUEUE_GRAPHICS_BIT ) ) )
        {
            graphics_queue_family_index = uint32{ i };
        }
    }

    if ( std::nullopt == graphics_queue_family_index )
    {
        spdlog::error( "No graphics queue family found" );
        return false;
    }

    auto const unique_queue_indices = std::set{
        graphics_queue_family_index.value( ),
    };

    auto queue_priorities   = std::vector{ 1.0F };
    auto queue_create_infos = std::vector< VkDeviceQueueCreateInfo >{ };

    utils::ignore( std::ranges::transform(
        unique_queue_indices,
        std::back_inserter( queue_create_infos ),
        DeviceQueueCreateInfo{ queue_priorities }
    ) );

    auto const device_extension_names = std::vector{
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
#if defined( __APPLE__ )
        "VK_KHR_portability_subset",
#endif
    };

    auto device_features              = VkPhysicalDeviceFeatures{ };
    device_features.samplerAnisotropy = VK_TRUE;

    auto const device_create_info = VkDeviceCreateInfo{
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .queueCreateInfoCount    = static_cast< uint32 >( queue_create_infos.size( ) ),
        .pQueueCreateInfos       = queue_create_infos.data( ),
        .enabledLayerCount       = 0,
        .ppEnabledLayerNames     = nullptr,
        .enabledExtensionCount   = static_cast< uint32 >( device_extension_names.size( ) ),
        .ppEnabledExtensionNames = device_extension_names.data( ),
        .pEnabledFeatures        = &device_features,
    };

    CHECK_VK( ::vkCreateDevice( physical_device_, &device_create_info, nullptr, &device_ ) );
    spdlog::debug( "vkCreateDevice()" );

    auto constexpr graphics_queue_index = uint32{ 0 };
    ::vkGetDeviceQueue(
        device_,
        graphics_queue_family_index.value( ),
        graphics_queue_index,
        &graphics_queue_
    );

    auto const color_format = VK_FORMAT_B8G8R8A8_SRGB;

    auto const attachments = std::vector{
        VkAttachmentDescription{
            .flags          = 0,
            .format         = color_format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        },
    };

    auto const color_attachment_refs = std::vector{
        VkAttachmentReference{
            .attachment = 0,
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
    };

    auto const subpasses = std::vector{
        VkSubpassDescription{
            .flags                   = 0,
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount    = 0,
            .pInputAttachments       = nullptr,
            .colorAttachmentCount    = static_cast< uint32 >( color_attachment_refs.size( ) ),
            .pColorAttachments       = color_attachment_refs.data( ),
            .pResolveAttachments     = nullptr,
            .pDepthStencilAttachment = nullptr,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments    = nullptr,
        },
    };

    auto const subpass_dependencies = std::vector{
        VkSubpassDependency{
            .srcSubpass      = VK_SUBPASS_EXTERNAL,
            .dstSubpass      = 0,
            .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask   = 0,
            .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0,
        },
    };

    auto const render_pass_create_info = VkRenderPassCreateInfo{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0,
        .attachmentCount = static_cast< uint32 >( attachments.size( ) ),
        .pAttachments    = attachments.data( ),
        .subpassCount    = static_cast< uint32 >( subpasses.size( ) ),
        .pSubpasses      = subpasses.data( ),
        .dependencyCount = static_cast< uint32 >( subpass_dependencies.size( ) ),
        .pDependencies   = subpass_dependencies.data( ),
    };

    CHECK_VK( ::vkCreateRenderPass( device_, &render_pass_create_info, nullptr, &render_pass_ ) );
    spdlog::debug( "vkCreateRenderPass()" );

    auto const descriptor_pool_sizes = std::vector{
        VkDescriptorPoolSize{
            .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
        },
    };

    auto const command_pool_create_info = VkCommandPoolCreateInfo{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphics_queue_family_index.value( ),
    };

    CHECK_VK( ::vkCreateCommandPool( device_, &command_pool_create_info, nullptr, &command_pool_ )
    );
    spdlog::debug( "vkCreateCommandPool()" );

    auto const cmd_buf_alloc_info = VkCommandBufferAllocateInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = command_pool_,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    CHECK_VK( ::vkAllocateCommandBuffers( device_, &cmd_buf_alloc_info, &command_buffer_ ) );
    spdlog::debug( "vkAllocateCommandBuffers()" );

    auto const fence_create_info = VkFenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    CHECK_VK( ::vkCreateFence( device_, &fence_create_info, nullptr, &graphics_queue_fence_ ) );
    spdlog::debug( "vkCreateFence()" );

    auto const external_color_image_info = VkExternalMemoryImageCreateInfo{
        .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext       = nullptr,
        .handleTypes = external_memory_handle_type,
    };
    auto const color_image_create_info = VkImageCreateInfo{
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext       = &external_color_image_info,
        .flags       = 0,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = color_format,
        .extent      = image_extents,
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
        .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    CHECK_VK( ::vkCreateImage( device_, &color_image_create_info, nullptr, &color_image_ ) );
    spdlog::debug( "vkCreateImage()" );

    auto color_image_mem_reqs = VkMemoryRequirements{ };
    ::vkGetImageMemoryRequirements( device_, color_image_, &color_image_mem_reqs );

    auto memory_props = VkPhysicalDeviceMemoryProperties{ };
    ::vkGetPhysicalDeviceMemoryProperties( physical_device_, &memory_props );
    auto const mem_prop_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    auto memory_type_index = std::optional< uint32 >{ };
    for ( auto i = 0u; i < memory_props.memoryTypeCount; ++i )
    {
        auto const type_is_suitable
            = ( 0 != ( color_image_mem_reqs.memoryTypeBits & ( 1u << i ) ) );
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

    auto const export_image_memory_info = VkExportMemoryAllocateInfo{
        .sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext       = nullptr,
        .handleTypes = external_memory_handle_type,
    };
    auto const color_image_alloc_info = VkMemoryAllocateInfo{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = &export_image_memory_info,
        .allocationSize  = color_image_mem_reqs.size,
        .memoryTypeIndex = memory_type_index.value( ),
    };
    CHECK_VK( ::vkAllocateMemory( device_, &color_image_alloc_info, nullptr, &color_image_memory_ )
    );
    spdlog::debug( "vkAllocateMemory()" );

    CHECK_VK( ::vkBindImageMemory( device_, color_image_, color_image_memory_, 0 ) );

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
    CHECK_VK(
        ::vkCreateImageView( device_, &color_image_view_create_info, nullptr, &color_image_view_ )
    );
    spdlog::debug( "vkCreateImageView()" );

    auto const framebuffer_create_info = VkFramebufferCreateInfo{
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0,
        .renderPass      = render_pass_,
        .attachmentCount = 1,
        .pAttachments    = &color_image_view_,
        .width           = image_extents.width,
        .height          = image_extents.height,
        .layers          = 1,
    };
    CHECK_VK( ::vkCreateFramebuffer( device_, &framebuffer_create_info, nullptr, &framebuffer_ ) );
    spdlog::debug( "vkCreateFramebuffer()" );

    spdlog::info( "Color image file descriptor: {}", color_image_fd_ );

    auto const push_constant_ranges = std::array{
        VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset     = 0,
            .size       = sizeof( model_uniforms_ ),
        },
        VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = sizeof( model_uniforms_ ),
            .size       = sizeof( display_uniforms_ ),
        }
    };

    auto const pipeline_layout_info = VkPipelineLayoutCreateInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .setLayoutCount         = 0,
        .pSetLayouts            = nullptr,
        .pushConstantRangeCount = static_cast< uint32 >( push_constant_ranges.size( ) ),
        .pPushConstantRanges    = push_constant_ranges.data( )
    };
    CHECK_VK( ::vkCreatePipelineLayout( device_, &pipeline_layout_info, nullptr, &pipeline_layout_ )
    );
    spdlog::debug( "vkCreatePipelineLayout()" );

    auto const vert_shader_path = config::spriv_shader_dir_path( ) / "triangle.vert.spv";
    auto const frag_shader_path = config::spriv_shader_dir_path( ) / "triangle.frag.spv";

    auto vert_shader_code = std::vector< uint32_t >{ };
    auto frag_shader_code = std::vector< uint32_t >{ };
    if ( ( !utils::get_binary_file_contents( vert_shader_path, vert_shader_code ) )
         || ( !utils::get_binary_file_contents( frag_shader_path, frag_shader_code ) ) )
    {
        return false;
    }

    auto const vert_shader_module_create_info = VkShaderModuleCreateInfo{
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext    = nullptr,
        .flags    = 0,
        .codeSize = vert_shader_code.size( ) * sizeof( uint32_t ),
        .pCode    = vert_shader_code.data( ),
    };
    auto* vert_shader_module = VkShaderModule{ };
    CHECK_VK( ::vkCreateShaderModule(
        device_,
        &vert_shader_module_create_info,
        nullptr,
        &vert_shader_module
    ) );

    auto const frag_shader_module_create_info = VkShaderModuleCreateInfo{
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext    = nullptr,
        .flags    = 0,
        .codeSize = frag_shader_code.size( ) * sizeof( uint32_t ),
        .pCode    = frag_shader_code.data( ),
    };
    auto* frag_shader_module = VkShaderModule{ };
    CHECK_VK( ::vkCreateShaderModule(
        device_,
        &frag_shader_module_create_info,
        nullptr,
        &frag_shader_module
    ) );

    auto const shader_stages = std::vector{
        VkPipelineShaderStageCreateInfo{
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0,
            .stage               = VK_SHADER_STAGE_VERTEX_BIT,
            .module              = vert_shader_module,
            .pName               = "main",
            .pSpecializationInfo = nullptr,
        },
        VkPipelineShaderStageCreateInfo{
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0,
            .stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module              = frag_shader_module,
            .pName               = "main",
            .pSpecializationInfo = nullptr,
        },
    };

    auto const vertex_input_info = VkPipelineVertexInputStateCreateInfo{
        .sType                         = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext                         = nullptr,
        .flags                         = 0,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions    = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions    = nullptr,
    };

    auto const input_assembly = VkPipelineInputAssemblyStateCreateInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    auto const viewport_state = VkPipelineViewportStateCreateInfo{
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = 0,
        .viewportCount = 1,
        .pViewports    = nullptr,
        .scissorCount  = 1,
        .pScissors     = nullptr,
    };

    auto const rasterizer = VkPipelineRasterizationStateCreateInfo{
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_BACK_BIT,
        .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0.0F,
        .depthBiasClamp          = 0.0F,
        .depthBiasSlopeFactor    = 0.0F,
        .lineWidth               = 1.0F,
    };

    auto const multisampling = VkPipelineMultisampleStateCreateInfo{
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable   = VK_FALSE,
        .minSampleShading      = 1.0F,
        .pSampleMask           = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable      = VK_FALSE,
    };

    auto const color_blend_attachment = VkPipelineColorBlendAttachmentState{
        .blendEnable         = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    auto const color_blending = VkPipelineColorBlendStateCreateInfo{
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments    = &color_blend_attachment,
        .blendConstants  = { 0.0F, 0.0F, 0.0F, 0.0F },
    };

    auto const dynamic_states = std::array{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    auto const dynamic_state  = VkPipelineDynamicStateCreateInfo{
         .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
         .pNext             = nullptr,
         .flags             = 0,
         .dynamicStateCount = static_cast< uint32 >( dynamic_states.size( ) ),
         .pDynamicStates    = dynamic_states.data( ),
    };

    auto const pipeline_create_info = VkGraphicsPipelineCreateInfo{
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = nullptr,
        .flags               = 0,
        .stageCount          = static_cast< uint32 >( shader_stages.size( ) ),
        .pStages             = shader_stages.data( ),
        .pVertexInputState   = &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState  = nullptr,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState   = &multisampling,
        .pDepthStencilState  = nullptr,
        .pColorBlendState    = &color_blending,
        .pDynamicState       = &dynamic_state,
        .layout              = pipeline_layout_,
        .renderPass          = render_pass_,
        .subpass             = 0,
        .basePipelineHandle  = nullptr,
        .basePipelineIndex   = -1,
    };
    CHECK_VK( ::vkCreateGraphicsPipelines(
        device_,
        nullptr,
        1,
        &pipeline_create_info,
        nullptr,
        &pipeline_
    ) );
    spdlog::debug( "vkCreateGraphicsPipelines()" );

    ::vkDestroyShaderModule( device_, frag_shader_module, nullptr );
    ::vkDestroyShaderModule( device_, vert_shader_module, nullptr );

    return true;
}

auto App::run( ) -> bool
{
    auto app_should_exit = false;
    spdlog::info( "Running render loop..." );
    spdlog::info( "Press Enter to exit." );

    auto const start_time = std::chrono::steady_clock::now( );

    using FloatSeconds = std::chrono::duration< float, std::chrono::seconds::period >;
    auto constexpr angular_velocity_rps = 0.5F;

    while ( !app_should_exit )
    {
        // Poll for any input
        if ( auto const processed_bytes
             = ::read( STDIN_FILENO, input_buffer_.data( ), input_buffer_.size( ) );
             processed_bytes > 0 )
        {
            spdlog::info( "Enter pressed." );
            app_should_exit = true;
        }
        else if ( ( processed_bytes < 0 ) && ( errno != EAGAIN ) )
        {
            spdlog::error( "read() failed: {}", std::strerror( errno ) );
            app_should_exit = true;
        }
        else
        {
            // The buffer is empty or EAGAIN was returned (implying non-blocking input checks)

            auto const current_duration = start_time - std::chrono::steady_clock::now( );
            auto const current_duration_s
                = std::chrono::duration_cast< FloatSeconds >( current_duration ).count( );

            model_uniforms_.scale_rotation_translation[ 1 ]
                = M_PI_2f * angular_velocity_rps * current_duration_s;
        }

        // Render pipeline here.
        auto const graphics_fences = std::array{ graphics_queue_fence_ };

        CHECK_VK( ::vkWaitForFences(
            device_,
            static_cast< uint32 >( graphics_fences.size( ) ),
            graphics_fences.data( ),
            VK_TRUE,
            max_possible_timeout
        ) );

        CHECK_VK( ::vkResetFences(
            device_,
            static_cast< uint32 >( graphics_fences.size( ) ),
            graphics_fences.data( )
        ) );

        CHECK_VK( ::vkResetCommandBuffer( command_buffer_, 0 ) );

        auto const begin_info = VkCommandBufferBeginInfo{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .pInheritanceInfo = nullptr,
        };
        CHECK_VK( ::vkBeginCommandBuffer( command_buffer_, &begin_info ) );

        auto const clear_values = std::array{
            VkClearValue{
                .color = VkClearColorValue{ .float32 = { 0.0F, 0.0F, 0.0F, 0.0F } },
            },
        };
        auto const render_pass_info = VkRenderPassBeginInfo{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext           = nullptr,
            .renderPass      = render_pass_,
            .framebuffer     = framebuffer_,
            .renderArea      = VkRect2D{
                .offset = VkOffset2D{ .x = 0, .y = 0 },
                .extent = {.width = image_extents.width, .height = image_extents.height },
            },
            .clearValueCount = static_cast< uint32 >( clear_values.size( ) ),
            .pClearValues    = clear_values.data( ),
        };

        ::vkCmdBeginRenderPass( command_buffer_, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE );
        ::vkCmdBindPipeline( command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_ );

        auto const viewport = VkViewport{
            .x        = 0.0F,
            .y        = 0.0F,
            .width    = static_cast< float32 >( image_extents.width ),
            .height   = static_cast< float32 >( image_extents.height ),
            .minDepth = 0.0F,
            .maxDepth = 1.0F,
        };
        auto constexpr first_viewport = 0;
        auto constexpr viewport_count = 1;
        ::vkCmdSetViewport( command_buffer_, first_viewport, viewport_count, &viewport );

        auto const scissors = VkRect2D{
            .offset = VkOffset2D{ .x = 0, .y = 0 },
            .extent = { .width = image_extents.width, .height = image_extents.height },
        };
        auto constexpr first_scissor = 0;
        auto constexpr scissor_count = 1;
        ::vkCmdSetScissor( command_buffer_, first_scissor, scissor_count, &scissors );

        ::vkCmdPushConstants(
            command_buffer_,
            pipeline_layout_,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof( model_uniforms_ ),
            &model_uniforms_
        );

        ::vkCmdPushConstants(
            command_buffer_,
            pipeline_layout_,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            sizeof( model_uniforms_ ),
            sizeof( display_uniforms_ ),
            &display_uniforms_
        );

        auto constexpr vertex_count   = 3;
        auto constexpr instance_count = 1;
        auto constexpr first_vertex   = 0;
        auto constexpr first_instance = 0;
        ::vkCmdDraw( command_buffer_, vertex_count, instance_count, first_vertex, first_instance );

        ::vkCmdEndRenderPass( command_buffer_ );

        CHECK_VK( ::vkEndCommandBuffer( command_buffer_ ) );

        auto const submit_info = VkSubmitInfo{
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = nullptr,
            .waitSemaphoreCount   = 0,
            .pWaitSemaphores      = nullptr,
            .pWaitDstStageMask    = nullptr,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &command_buffer_,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores    = nullptr,
        };

        auto constexpr submit_count = 1;
        CHECK_VK(
            ::vkQueueSubmit( graphics_queue_, submit_count, &submit_info, graphics_queue_fence_ )
        );

        if ( -1 == color_image_fd_ )
        {
            CHECK_VK( ::vkDeviceWaitIdle( device_ ) );

            auto* const vkGetMemoryFdKHR = reinterpret_cast< PFN_vkGetMemoryFdKHR >(
                ::vkGetInstanceProcAddr( instance_, "vkGetMemoryFdKHR" )
            );
            if ( nullptr == vkGetMemoryFdKHR )
            {
                spdlog::error( "vkGetInstanceProcAddr() failed" );
                return false;
            }

            auto const memory_info = VkMemoryGetFdInfoKHR{
                .sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
                .pNext      = nullptr,
                .memory     = color_image_memory_,
                .handleType = external_memory_handle_type,
            };
            CHECK_VK( vkGetMemoryFdKHR( device_, &memory_info, &color_image_fd_ ) );
            spdlog::debug( "vkGetMemoryFdKHR()" );

            if ( !socket_.initialize( ) )
            {
                return false;
            }
            if ( !socket_.connect_and_send( "socket", color_image_fd_ ) )
            {
                return false;
            }
        }
    }

    CHECK_VK( ::vkDeviceWaitIdle( device_ ) );
    spdlog::info( "Exiting..." );
    return true;
}

} // namespace ltb

auto main( ltb::int32 const argc, char const* const argv[] ) -> ltb::int32
{
    spdlog::set_level( spdlog::level::trace );

    auto physical_device_index = ltb::uint32{ 0 };
    if ( argc > 1 )
    {
        auto const* const start = argv[ 1 ];
        auto const* const end   = argv[ 1 ] + std::strlen( argv[ 1 ] );

        if ( auto const result = std::from_chars( start, end, physical_device_index );
             std::errc( ) != result.ec )
        {
            spdlog::error( "Invalid argument: {}", argv[ 1 ] );
            return 1;
        }
    }

    if ( auto app = ltb::App( ); app.initialize( physical_device_index ) )
    {
        app.run( );
    }

    spdlog::info( "Done." );
    return 0;
}
