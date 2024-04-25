// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////

// project
#include "ltb/net/fd_socket.hpp"
#include "ltb/utils/read_file.hpp"
#include "ltb/ltb_config.hpp"
#include "ltb/utils/ignore.hpp"

// external
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

// standard
#include <algorithm>
#include <charconv>
#include <optional>
#include <ranges>
#include <set>
#include <vector>

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

constexpr auto max_frames_in_flight = uint32_t{ 2 };
constexpr auto max_possible_timeout = std::numeric_limits< uint64_t >::max( );

auto default_error_callback( int32 const error, char const* description ) -> void
{
    spdlog::error( "GLFW Error ({}): {}", error, description );
}

auto log_error_on_resize( GLFWwindow* const, int32 const width, int32 const height )
{
    spdlog::error( "Framebuffer resized: {}x{}", width, height );
}

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
    // Glfw
    int32       glfw_   = GLFW_FALSE;
    GLFWwindow* window_ = { };

    // Initialization
    VkInstance                          instance_                       = { };
    PFN_vkCreateDebugUtilsMessengerEXT  vkCreateDebugUtilsMessengerEXT  = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
    VkDebugUtilsMessengerEXT            debug_messenger_                = { };
    VkSurfaceKHR                        surface_                        = { };
    VkPhysicalDevice                    physical_device_                = { };
    VkDevice                            device_                         = { };
    VkQueue                             graphics_queue_                 = { };
    VkQueue                             surface_queue_                  = { };
    VkRenderPass                        render_pass_                    = { };

    // Pools
    VkDescriptorPool descriptor_pool_ = { };
    VkCommandPool    command_pool_    = { };

    // Synchronization objects
    std::vector< VkCommandBuffer > command_buffers_            = { };
    std::vector< VkSemaphore >     image_available_semaphores_ = { };
    std::vector< VkSemaphore >     render_finished_semaphores_ = { };
    std::vector< VkFence >         graphics_queue_fences_      = { };
    uint32                         current_frame_              = 0;

    // Output
    VkSwapchainKHR               swapchain_             = { };
    std::vector< VkImage >       swapchain_images_      = { };
    std::vector< VkImageView >   swapchain_image_views_ = { };
    std::vector< VkFramebuffer > framebuffers_          = { };
    VkExtent2D                   framebuffer_size_      = { };

    // Pipeline
    VkDescriptorSetLayout descriptor_set_layout_ = { };
    VkPipelineLayout      pipeline_layout_       = { };
    VkPipeline            pipeline_              = { };

    // Imported Image
    VkImage                        color_image_         = { };
    VkDeviceMemory                 color_image_memory_  = { };
    VkImageView                    color_image_view_    = { };
    VkSampler                      color_image_sampler_ = { };
    std::vector< VkDescriptorSet > descriptor_sets_     = { };

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
        ::vkDestroySampler( device_, color_image_sampler_, nullptr );
        spdlog::debug( "vkDestroySampler()" );
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

    if ( nullptr != pipeline_ )
    {
        ::vkDestroyPipeline( device_, pipeline_, nullptr );
        spdlog::debug( "vkDestroyPipeline()" );
    }

    if ( nullptr != descriptor_set_layout_ )
    {
        ::vkDestroyDescriptorSetLayout( device_, descriptor_set_layout_, nullptr );
        spdlog::debug( "vkDestroyDescriptorSetLayout()" );
    }

    if ( nullptr != pipeline_layout_ )
    {
        ::vkDestroyPipelineLayout( device_, pipeline_layout_, nullptr );
        spdlog::debug( "vkDestroyPipelineLayout()" );
    }

    for ( auto* const framebuffer : framebuffers_ )
    {
        ::vkDestroyFramebuffer( device_, framebuffer, nullptr );
    }
    spdlog::debug( "vkDestroyFramebuffer()x{}", framebuffers_.size( ) );
    framebuffers_.clear( );

    for ( auto* const image_view : swapchain_image_views_ )
    {
        ::vkDestroyImageView( device_, image_view, nullptr );
    }
    spdlog::debug( "vkDestroyImageView()x{}", swapchain_image_views_.size( ) );
    swapchain_image_views_.clear( );

    swapchain_images_.clear( );
    if ( nullptr != swapchain_ )
    {
        ::vkDestroySwapchainKHR( device_, swapchain_, nullptr );
        spdlog::debug( "vkDestroySwapchainKHR()" );
    }

    for ( auto* const fence : graphics_queue_fences_ )
    {
        ::vkDestroyFence( device_, fence, nullptr );
    }
    spdlog::debug( "vkDestroyFence()x{}", graphics_queue_fences_.size( ) );
    graphics_queue_fences_.clear( );

    for ( auto* const semaphore : render_finished_semaphores_ )
    {
        ::vkDestroySemaphore( device_, semaphore, nullptr );
    }
    spdlog::debug( "vkDestroySemaphore()x{}", render_finished_semaphores_.size( ) );
    render_finished_semaphores_.clear( );

    for ( auto* const semaphore : image_available_semaphores_ )
    {
        ::vkDestroySemaphore( device_, semaphore, nullptr );
    }
    spdlog::debug( "vkDestroySemaphore()x{}", image_available_semaphores_.size( ) );
    image_available_semaphores_.clear( );

    if ( !command_buffers_.empty( ) )
    {
        ::vkFreeCommandBuffers(
            device_,
            command_pool_,
            static_cast< uint32 >( command_buffers_.size( ) ),
            command_buffers_.data( )
        );
        spdlog::debug( "vkFreeCommandBuffers()" );
    }

    if ( nullptr != command_pool_ )
    {
        ::vkDestroyCommandPool( device_, command_pool_, nullptr );
        spdlog::debug( "vkDestroyCommandPool()" );
    }

    if ( nullptr != descriptor_pool_ )
    {
        ::vkDestroyDescriptorPool( device_, descriptor_pool_, nullptr );
        spdlog::debug( "vkDestroyDescriptorPool()" );
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

    if ( nullptr != surface_ )
    {
        ::vkDestroySurfaceKHR( instance_, surface_, nullptr );
        spdlog::debug( "vkDestroySurfaceKHR()" );
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

    if ( nullptr != window_ )
    {
        ::glfwDestroyWindow( window_ );
        spdlog::debug( "glfwDestroyWindow()" );
    }

    if ( GLFW_TRUE == glfw_ )
    {
        ::glfwTerminate( );
        spdlog::debug( "glfwTerminate()" );
    }
}

auto App::initialize( uint32 const physical_device_index ) -> bool
{
    utils::ignore( ::glfwSetErrorCallback( default_error_callback ) );

    if ( glfw_ = ::glfwInit( ); GLFW_FALSE == glfw_ )
    {
        spdlog::error( "glfwInit() failed" );
        return false;
    }
    spdlog::debug( "glfwInit()" );

    if ( GLFW_FALSE == ::glfwVulkanSupported( ) )
    {
        spdlog::error( "glfwVulkanSupported() failed" );
        return false;
    }
    spdlog::debug( "glfwVulkanSupported()" );

    auto* const primary_monitor = ::glfwGetPrimaryMonitor( );
    if ( nullptr == primary_monitor )
    {
        spdlog::error( "glfwGetPrimaryMonitor() failed" );
        return false;
    }
    auto const* const video_mode = ::glfwGetVideoMode( primary_monitor );
    if ( nullptr == video_mode )
    {
        spdlog::error( "glfwGetVideoMode() failed" );
        return false;
    }

    ::glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );
    ::glfwWindowHint( GLFW_RED_BITS, video_mode->redBits );
    ::glfwWindowHint( GLFW_GREEN_BITS, video_mode->greenBits );
    ::glfwWindowHint( GLFW_BLUE_BITS, video_mode->blueBits );
    ::glfwWindowHint( GLFW_REFRESH_RATE, video_mode->refreshRate );
    ::glfwWindowHint( GLFW_RESIZABLE, GLFW_FALSE );

    if ( window_ = ::glfwCreateWindow(
             video_mode->width,
             video_mode->height,
             "Vulkan Application",
#if defined( __APPLE__ )
             // Passing the primary monitor doesn't work on macOS?
             nullptr,
#else
             primary_monitor,
#endif
             nullptr
         );
         nullptr == window_ )
    {
        spdlog::error( "glfwCreateWindow() failed" );
        return false;
    }
    spdlog::debug( "glfwCreateWindow()" );

    // Log when the window is resized (this should never happen).
    utils::ignore( ::glfwSetFramebufferSizeCallback( window_, log_error_on_resize ) );

    auto extension_names = std::vector{
#if defined( __APPLE__ )
        "VK_KHR_portability_enumeration",
#endif
#if !defined( NDEBUG )
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
    };

    auto              instance_extension_count = uint32_t{ 0 };
    auto const* const glfw_extensions
        = ::glfwGetRequiredInstanceExtensions( &instance_extension_count );

    utils::ignore( extension_names.insert(
        extension_names.end( ),
        glfw_extensions,
        glfw_extensions + instance_extension_count
    ) );

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
        .flags           = 0U,
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

    CHECK_VK( ::glfwCreateWindowSurface( instance_, window_, nullptr, &surface_ ) );
    spdlog::debug( "glfwCreateWindowSurface()" );

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
    auto surface_queue_family_index  = std::optional< uint32 >{ };

    for ( auto i = uint32{ 0 }; i < queue_family_count; ++i )
    {
        if ( ( !graphics_queue_family_index ) || ( !surface_queue_family_index ) )
        {
            if ( 0 != ( queue_families[ i ].queueFlags & VK_QUEUE_GRAPHICS_BIT ) )
            {
                graphics_queue_family_index = i;
            }

            auto surface_support = VkBool32{ false };
            CHECK_VK( ::vkGetPhysicalDeviceSurfaceSupportKHR(
                physical_device_,
                i,
                surface_,
                &surface_support
            ) );

            if ( VK_TRUE == surface_support )
            {
                surface_queue_family_index = i;
            }
        }
    }

    if ( std::nullopt == graphics_queue_family_index )
    {
        spdlog::error( "No graphics queue family found" );
        return false;
    }
    if ( std::nullopt == surface_queue_family_index )
    {
        spdlog::error( "No surface queue family found" );
        return false;
    }

    auto const unique_queue_indices = std::set{
        graphics_queue_family_index.value( ),
        surface_queue_family_index.value( ),
    };

    auto queue_priorities   = std::vector{ 1.0F };
    auto queue_create_infos = std::vector< VkDeviceQueueCreateInfo >{ };

    utils::ignore( std::ranges::transform(
        unique_queue_indices,
        std::back_inserter( queue_create_infos ),
        DeviceQueueCreateInfo{ queue_priorities }
    ) );

    auto const device_extension_names = std::vector{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
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
        .flags                   = 0U,
        .queueCreateInfoCount    = static_cast< uint32 >( queue_create_infos.size( ) ),
        .pQueueCreateInfos       = queue_create_infos.data( ),
        .enabledLayerCount       = 0U,
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

    auto constexpr surface_queue_index = uint32{ 0 };
    ::vkGetDeviceQueue(
        device_,
        surface_queue_family_index.value( ),
        surface_queue_index,
        &surface_queue_
    );

    auto physical_device_formats_count = uint32{ 0 };
    CHECK_VK( ::vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device_,
        surface_,
        &physical_device_formats_count,
        nullptr
    ) );
    auto surface_formats = std::vector< VkSurfaceFormatKHR >( physical_device_formats_count );
    CHECK_VK( ::vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device_,
        surface_,
        &physical_device_formats_count,
        surface_formats.data( )
    ) );

    auto preferred_surface_format = VkSurfaceFormatKHR{
        .format     = VK_FORMAT_B8G8R8A8_SRGB,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    };

    if ( !std::ranges::any_of( surface_formats, SurfaceFormatEquals{ preferred_surface_format } ) )
    {
        preferred_surface_format = surface_formats[ 0U ];
    }

    auto const attachments = std::vector{
        VkAttachmentDescription{
            .flags          = 0U,
            .format         = preferred_surface_format.format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },
    };

    auto const color_attachment_refs = std::vector{
        VkAttachmentReference{
            .attachment = 0U,
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
    };

    auto const subpasses = std::vector{
        VkSubpassDescription{
            .flags                   = 0U,
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount    = 0U,
            .pInputAttachments       = nullptr,
            .colorAttachmentCount    = static_cast< uint32 >( color_attachment_refs.size( ) ),
            .pColorAttachments       = color_attachment_refs.data( ),
            .pResolveAttachments     = nullptr,
            .pDepthStencilAttachment = nullptr,
            .preserveAttachmentCount = 0U,
            .pPreserveAttachments    = nullptr,
        },
    };

    auto const subpass_dependencies = std::vector{
        VkSubpassDependency{
            .srcSubpass      = VK_SUBPASS_EXTERNAL,
            .dstSubpass      = 0U,
            .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask   = 0U,
            .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0U,
        },
    };

    auto const render_pass_create_info = VkRenderPassCreateInfo{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0U,
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
            .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = max_frames_in_flight,
        },
    };
    auto const descriptor_pool_create_info = VkDescriptorPoolCreateInfo{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = 0U,
        .maxSets       = max_frames_in_flight,
        .poolSizeCount = static_cast< uint32 >( descriptor_pool_sizes.size( ) ),
        .pPoolSizes    = descriptor_pool_sizes.data( ),
    };
    CHECK_VK( ::vkCreateDescriptorPool(
        device_,
        &descriptor_pool_create_info,
        nullptr,
        &descriptor_pool_
    ) );
    spdlog::debug( "vkCreateDescriptorPool()" );

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
        .commandBufferCount = max_frames_in_flight,
    };
    command_buffers_.resize( cmd_buf_alloc_info.commandBufferCount );
    CHECK_VK( ::vkAllocateCommandBuffers( device_, &cmd_buf_alloc_info, command_buffers_.data( ) )
    );
    spdlog::debug( "vkAllocateCommandBuffers()" );

    auto const semaphore_create_info = VkSemaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
    };

    image_available_semaphores_.resize( max_frames_in_flight );
    render_finished_semaphores_.resize( max_frames_in_flight );

    for ( auto i = 0U; i < max_frames_in_flight; ++i )
    {
        CHECK_VK( ::vkCreateSemaphore(
            device_,
            &semaphore_create_info,
            nullptr,
            image_available_semaphores_.data( ) + i
        ) );
        CHECK_VK( ::vkCreateSemaphore(
            device_,
            &semaphore_create_info,
            nullptr,
            render_finished_semaphores_.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateSemaphore()x{}", max_frames_in_flight );
    spdlog::debug( "vkCreateSemaphore()x{}", max_frames_in_flight );

    auto const fence_create_info = VkFenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    graphics_queue_fences_.resize( max_frames_in_flight );
    for ( auto i = 0U; i < max_frames_in_flight; ++i )
    {
        CHECK_VK( ::vkCreateFence(
            device_,
            &fence_create_info,
            nullptr,
            graphics_queue_fences_.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateFence()x{}", max_frames_in_flight );

    auto framebuffer_width  = int32{ 0 };
    auto framebuffer_height = int32{ 0 };
    ::glfwGetFramebufferSize( window_, &framebuffer_width, &framebuffer_height );

    auto surface_capabilities = VkSurfaceCapabilitiesKHR{ };
    CHECK_VK( ::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device_,
        surface_,
        &surface_capabilities
    ) );

    framebuffer_size_ = VkExtent2D{
        std::clamp(
            static_cast< uint32 >( framebuffer_width ),
            surface_capabilities.minImageExtent.width,
            surface_capabilities.maxImageExtent.width
        ),
        std::clamp(
            static_cast< uint32 >( framebuffer_height ),
            surface_capabilities.minImageExtent.height,
            surface_capabilities.maxImageExtent.height
        ),
    };

    auto min_image_count = surface_capabilities.minImageCount + 1U;

    // don't exceed the max (zero means no maximum).
    if ( ( surface_capabilities.maxImageCount > 0U )
         && ( min_image_count > surface_capabilities.maxImageCount ) )
    {
        min_image_count = surface_capabilities.maxImageCount;
    }

    auto const queue_family_indices
        = std::vector< uint32 >( unique_queue_indices.begin( ), unique_queue_indices.end( ) );

    auto const concurrency               = ( queue_family_indices.size( ) > 1 );
    auto const unique_queue_family_count = static_cast< uint32 >( queue_family_indices.size( ) );

    auto const swapchain_create_info = VkSwapchainCreateInfoKHR{
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext            = nullptr,
        .flags            = 0U,
        .surface          = surface_,
        .minImageCount    = min_image_count,
        .imageFormat      = preferred_surface_format.format,
        .imageColorSpace  = preferred_surface_format.colorSpace,
        .imageExtent      = framebuffer_size_,
        .imageArrayLayers = 1U,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode
        = ( concurrency ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE ),
        .queueFamilyIndexCount = ( concurrency ? unique_queue_family_count : 0 ),
        .pQueueFamilyIndices   = queue_family_indices.data( ),
        .preTransform          = surface_capabilities.currentTransform,
        .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode           = VK_PRESENT_MODE_FIFO_KHR,
        .clipped               = VK_TRUE,
        .oldSwapchain          = nullptr,
    };

    CHECK_VK( ::vkCreateSwapchainKHR( device_, &swapchain_create_info, nullptr, &swapchain_ ) );
    spdlog::debug( "vkCreateSwapchainKHR()" );

    auto swapchain_image_count = uint32{ 0 };
    CHECK_VK( ::vkGetSwapchainImagesKHR( device_, swapchain_, &swapchain_image_count, nullptr ) );
    swapchain_images_.resize( swapchain_image_count );
    CHECK_VK( ::vkGetSwapchainImagesKHR(
        device_,
        swapchain_,
        &swapchain_image_count,
        swapchain_images_.data( )
    ) );
    spdlog::debug( "vkGetSwapchainImagesKHR()" );

    swapchain_image_views_.resize( swapchain_image_count );
    for ( auto i = 0U; i < swapchain_image_count; ++i )
    {
        auto const image_view_create_info = VkImageViewCreateInfo{
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = 0U,
            .image            = swapchain_images_[ i ],
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = preferred_surface_format.format,
            .components       = VkComponentMapping{
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = VkImageSubresourceRange{
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0U,
                .levelCount     = 1U,
                .baseArrayLayer = 0U,
                .layerCount     = 1U,
            },
        };
        CHECK_VK( ::vkCreateImageView(
            device_,
            &image_view_create_info,
            nullptr,
            swapchain_image_views_.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateImageView()x{}", swapchain_image_views_.size( ) );

    framebuffers_.resize( swapchain_image_count );
    for ( auto i = 0U; i < swapchain_image_count; ++i )
    {
        auto const framebuffer_create_info = VkFramebufferCreateInfo{
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = nullptr,
            .flags           = 0U,
            .renderPass      = render_pass_,
            .attachmentCount = 1U,
            .pAttachments    = swapchain_image_views_.data( ) + i,
            .width           = framebuffer_size_.width,
            .height          = framebuffer_size_.height,
            .layers          = 1U,
        };
        CHECK_VK( ::vkCreateFramebuffer(
            device_,
            &framebuffer_create_info,
            nullptr,
            framebuffers_.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateFramebuffer()x{}", framebuffers_.size( ) );

    auto const descriptor_set_layout_bindings = std::array{
        VkDescriptorSetLayoutBinding{
            .binding            = 1U,
            .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount    = 1U,
            .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        },
    };
    auto const descriptor_set_layout_info = VkDescriptorSetLayoutCreateInfo{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = nullptr,
        .flags        = 0U,
        .bindingCount = static_cast< uint32 >( descriptor_set_layout_bindings.size( ) ),
        .pBindings    = descriptor_set_layout_bindings.data( ),
    };
    CHECK_VK( ::vkCreateDescriptorSetLayout(
        device_,
        &descriptor_set_layout_info,
        nullptr,
        &descriptor_set_layout_
    ) );

    auto const pipeline_layout_info = VkPipelineLayoutCreateInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0U,
        .setLayoutCount         = 1U,
        .pSetLayouts            = &descriptor_set_layout_,
        .pushConstantRangeCount = 0U,
        .pPushConstantRanges    = nullptr,
    };
    CHECK_VK( ::vkCreatePipelineLayout( device_, &pipeline_layout_info, nullptr, &pipeline_layout_ )
    );
    spdlog::debug( "vkCreatePipelineLayout()" );

    auto const vert_shader_path = config::spriv_shader_dir_path( ) / "composite.vert.spv";
    auto const frag_shader_path = config::spriv_shader_dir_path( ) / "composite.frag.spv";

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
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
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

    CHECK_VK( ::vkCreateImage( device_, &color_image_create_info, nullptr, &color_image_ ) );
    spdlog::debug( "vkCreateImage()" );

    auto color_image_mem_reqs = VkMemoryRequirements{ };
    ::vkGetImageMemoryRequirements( device_, color_image_, &color_image_mem_reqs );

    auto memory_props = VkPhysicalDeviceMemoryProperties{ };
    ::vkGetPhysicalDeviceMemoryProperties( physical_device_, &memory_props );
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
    CHECK_VK( ::vkAllocateMemory( device_, &color_image_alloc_info, nullptr, &color_image_memory_ )
    );
    spdlog::debug( "vkAllocateMemory()" );

    auto constexpr memory_offset = VkDeviceSize{ 0 };
    CHECK_VK( ::vkBindImageMemory( device_, color_image_, color_image_memory_, memory_offset ) );
    spdlog::debug( "vkBindImageMemory()" );

    auto const command_buffer_alloc_info = VkCommandBufferAllocateInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = command_pool_,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1U,
    };

    auto* command_buffer = VkCommandBuffer{ };
    CHECK_VK( ::vkAllocateCommandBuffers( device_, &command_buffer_alloc_info, &command_buffer ) );

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
    CHECK_VK( ::vkQueueSubmit( graphics_queue_, 1, &submit_info, nullptr ) );
    CHECK_VK( ::vkQueueWaitIdle( graphics_queue_ ) );

    ::vkFreeCommandBuffers( device_, command_pool_, 1U, &command_buffer );

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
    CHECK_VK( ::vkCreateSampler( device_, &sampler_info, nullptr, &color_image_sampler_ ) );

    auto const layouts
        = std::vector< VkDescriptorSetLayout >( max_frames_in_flight, descriptor_set_layout_ );
    auto const descriptor_set_allocate_info = VkDescriptorSetAllocateInfo{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorPool     = descriptor_pool_,
        .descriptorSetCount = max_frames_in_flight,
        .pSetLayouts        = layouts.data( ),
    };

    descriptor_sets_.resize( descriptor_set_allocate_info.descriptorSetCount );
    CHECK_VK( ::vkAllocateDescriptorSets(
        device_,
        &descriptor_set_allocate_info,
        descriptor_sets_.data( )
    ) );

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
                .dstSet           = descriptor_sets_[ i ],
                .dstBinding       = 1U,
                .dstArrayElement  = 0U,
                .descriptorCount  = 1U,
                .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo       = &image_info,
                .pBufferInfo      = nullptr,
                .pTexelBufferView = nullptr,
            },
        };

        ::vkUpdateDescriptorSets(
            device_,
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
        auto* const graphics_queue_fence = graphics_queue_fences_[ current_frame_ ];

        auto const graphics_fences = std::array{ graphics_queue_fence };

        CHECK_VK( ::vkWaitForFences(
            device_,
            static_cast< uint32 >( graphics_fences.size( ) ),
            graphics_fences.data( ),
            VK_TRUE,
            max_possible_timeout
        ) );

        auto* const image_available_semaphore = image_available_semaphores_[ current_frame_ ];

        auto swapchain_image_index = uint32{ 0 };
        CHECK_VK( ::vkAcquireNextImageKHR(
            device_,
            swapchain_,
            max_possible_timeout,
            image_available_semaphore,
            nullptr,
            &swapchain_image_index
        ) );
        auto* const framebuffer = framebuffers_[ swapchain_image_index ];

        CHECK_VK( ::vkResetFences(
            device_,
            static_cast< uint32 >( graphics_fences.size( ) ),
            graphics_fences.data( )
        ) );

        auto* const command_buffer = command_buffers_[ current_frame_ ];
        auto constexpr reset_flags = VkCommandBufferResetFlags{ 0U };
        CHECK_VK( ::vkResetCommandBuffer( command_buffer, reset_flags ) );

        auto const begin_info = VkCommandBufferBeginInfo{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .pInheritanceInfo = nullptr,
        };
        CHECK_VK( ::vkBeginCommandBuffer( command_buffer, &begin_info ) );

        auto const clear_values = std::array{
            VkClearValue{
                .color = VkClearColorValue{ .float32 = { 0.0F, 0.0F, 0.0F, 0.0F } },
            },
        };
        auto const render_pass_info = VkRenderPassBeginInfo{
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext           = nullptr,
            .renderPass      = render_pass_,
            .framebuffer     = framebuffer,
            .renderArea      = VkRect2D{
                 .offset = VkOffset2D{ .x = 0, .y = 0 },
                 .extent = framebuffer_size_,
            },
            .clearValueCount = static_cast< uint32 >( clear_values.size( ) ),
            .pClearValues    = clear_values.data( ),
        };
        ::vkCmdBeginRenderPass( command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE );
        ::vkCmdBindPipeline( command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_ );

        auto const viewport = VkViewport{
            .x        = 0.0F,
            .y        = 0.0F,
            .width    = static_cast< float32 >( framebuffer_size_.width ),
            .height   = static_cast< float32 >( framebuffer_size_.height ),
            .minDepth = 0.0F,
            .maxDepth = 1.0F,
        };
        auto constexpr first_viewport = 0U;
        auto constexpr viewport_count = 1U;
        ::vkCmdSetViewport( command_buffer, first_viewport, viewport_count, &viewport );

        auto const scissors = VkRect2D{
            .offset = VkOffset2D{ .x = 0, .y = 0 },
            .extent = framebuffer_size_,
        };
        auto constexpr first_scissor = 0U;
        auto constexpr scissor_count = 1U;
        ::vkCmdSetScissor( command_buffer, first_scissor, scissor_count, &scissors );

        auto constexpr first_set            = 0U;
        auto constexpr descriptor_set_count = 1U;
        auto constexpr dynamic_offset_count = 0U;
        auto constexpr dynamic_offsets      = nullptr;
        ::vkCmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_,
            first_set,
            descriptor_set_count,
            &descriptor_sets_[ current_frame_ ],
            dynamic_offset_count,
            dynamic_offsets
        );

        auto constexpr vertex_count   = 4U;
        auto constexpr instance_count = 1U;
        auto constexpr first_vertex   = 0U;
        auto constexpr first_instance = 0U;
        ::vkCmdDraw( command_buffer, vertex_count, instance_count, first_vertex, first_instance );

        ::vkCmdEndRenderPass( command_buffer );

        CHECK_VK( ::vkEndCommandBuffer( command_buffer ) );

        auto* const render_finished_semaphore = render_finished_semaphores_[ current_frame_ ];
        auto constexpr semaphore_stage
            = VkPipelineStageFlags{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        auto const submit_info = VkSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            // Color attachment stage must wait for the image acquisition to finish.
            .waitSemaphoreCount = 1U,
            .pWaitSemaphores    = &image_available_semaphore,
            .pWaitDstStageMask  = &semaphore_stage,
            // The commands being submitted to the graphics device.
            .commandBufferCount = 1U,
            .pCommandBuffers    = &command_buffer,
            // Signal the render finished semaphore so future
            // commands waiting on this step can proceed.
            .signalSemaphoreCount = 1U,
            .pSignalSemaphores    = &render_finished_semaphore,
        };

        // Use the fence to block future CPU code that also references this fence.
        auto constexpr submit_count = 1;
        CHECK_VK(
            ::vkQueueSubmit( graphics_queue_, submit_count, &submit_info, graphics_queue_fence )
        );

        auto const present_info = VkPresentInfoKHR{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            // Wait for the render to finish before presenting.
            .waitSemaphoreCount = 1U,
            .pWaitSemaphores    = &render_finished_semaphore,
            .swapchainCount     = 1U,
            .pSwapchains        = &swapchain_,
            .pImageIndices      = &swapchain_image_index,
            .pResults           = nullptr,
        };
        CHECK_VK( ::vkQueuePresentKHR( surface_queue_, &present_info ) );

        current_frame_ = ( current_frame_ + 1U ) % max_frames_in_flight;

        // This GLFW_KEY_ESCAPE bit shouldn't exist in a final product.
        should_exit = ( GLFW_TRUE == ::glfwWindowShouldClose( window_ ) )
                   || ( GLFW_PRESS == ::glfwGetKey( window_, GLFW_KEY_ESCAPE ) );
    }

    CHECK_VK( ::vkDeviceWaitIdle( device_ ) );

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
