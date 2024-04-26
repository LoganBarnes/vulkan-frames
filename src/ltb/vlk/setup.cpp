// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#include "ltb/vlk/setup.hpp"

// project
#include "ltb/utils/ignore.hpp"
#include "ltb/vlk/check.hpp"

// external
#include <spdlog/spdlog.h>

// standard
#include <algorithm>
#include <optional>
#include <ranges>
#include <set>

// platform
#include <fcntl.h>

namespace ltb::vlk
{
namespace
{

auto default_error_callback( int32 const error, char const* description ) -> void
{
    spdlog::error( "GLFW Error ({}): {}", error, description );
}

auto log_error_on_resize( GLFWwindow* const, int32 const width, int32 const height )
{
    spdlog::error( "Framebuffer resized: {}x{}", width, height );
}

auto initialize_glfw( int32& glfw, GLFWwindow*& window )
{
    utils::ignore( ::glfwSetErrorCallback( default_error_callback ) );

    if ( glfw = ::glfwInit( ); GLFW_FALSE == glfw )
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

    if ( window = ::glfwCreateWindow(
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
         nullptr == window )
    {
        spdlog::error( "glfwCreateWindow() failed" );
        return false;
    }
    spdlog::debug( "glfwCreateWindow()" );

    // Log when the window is resized (this should never happen).
    utils::ignore( ::glfwSetFramebufferSizeCallback( window, log_error_on_resize ) );

    return true;
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

auto initialize_instance(
    VkInstance&                          instance,
    PFN_vkDestroyDebugUtilsMessengerEXT& vkDestroyDebugUtilsMessengerEXT,
    VkDebugUtilsMessengerEXT&            debug_messenger,
    std::vector< char const* > const&    extra_extension_names
)
{
    auto extension_names = std::vector{
#if !defined( NDEBUG )
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
    };
    utils::ignore( extension_names.insert(
        extension_names.end( ),
        extra_extension_names.begin( ),
        extra_extension_names.end( )
    ) );

    auto constexpr layer_names = std::array{
#if !defined( NDEBUG )
        "VK_LAYER_KHRONOS_validation",
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
#if !defined( NDEBUG )
        .pfnUserCallback = default_debug_callback,
#else
        .pfnUserCallback = nullptr,
#endif
        .pUserData = nullptr,
    };

    auto const create_info = VkInstanceCreateInfo{
        .sType               = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext               = ( debug_create_info.pfnUserCallback ? &debug_create_info : nullptr ),
        .flags               = 0U,
        .pApplicationInfo    = &application_info,
        .enabledLayerCount   = static_cast< uint32 >( layer_names.size( ) ),
        .ppEnabledLayerNames = layer_names.data( ),
        .enabledExtensionCount   = static_cast< uint32 >( extension_names.size( ) ),
        .ppEnabledExtensionNames = extension_names.data( ),
    };

    CHECK_VK( ::vkCreateInstance( &create_info, nullptr, &instance ) );
    spdlog::debug( "vkCreateInstance()" );

    if ( nullptr != debug_create_info.pfnUserCallback )
    {
        auto const vkCreateDebugUtilsMessengerEXT
            = reinterpret_cast< PFN_vkCreateDebugUtilsMessengerEXT >(
                ::vkGetInstanceProcAddr( instance, "vkCreateDebugUtilsMessengerEXT" )
            );

        vkDestroyDebugUtilsMessengerEXT = reinterpret_cast< PFN_vkDestroyDebugUtilsMessengerEXT >(
            ::vkGetInstanceProcAddr( instance, "vkDestroyDebugUtilsMessengerEXT" )
        );

        if ( ( nullptr == vkCreateDebugUtilsMessengerEXT )
             || ( nullptr == vkDestroyDebugUtilsMessengerEXT ) )
        {
            spdlog::error( "vkGetInstanceProcAddr() failed" );
            return false;
        }

        CHECK_VK( vkCreateDebugUtilsMessengerEXT(
            instance,
            &debug_create_info,
            nullptr,
            &debug_messenger
        ) );
        spdlog::debug( "vkCreateDebugUtilsMessengerEXT()" );
    }

    return true;
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

auto initialize_physical_device(
    uint32 const        physical_device_index,
    VkInstance const&   instance,
    VkPhysicalDevice&   physical_device,
    uint32&             graphics_queue_family_index_out,
    VkSurfaceKHR const& optional_surface,
    uint32*             optional_surface_queue_family_index_out
)
{
    auto physical_device_count = uint32{ 0 };
    CHECK_VK( ::vkEnumeratePhysicalDevices( instance, &physical_device_count, nullptr ) );
    auto physical_devices = std::vector< VkPhysicalDevice >( physical_device_count );
    CHECK_VK(
        ::vkEnumeratePhysicalDevices( instance, &physical_device_count, physical_devices.data( ) )
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

    physical_device = physical_devices[ physical_device_index ];

    auto physical_device_properties = VkPhysicalDeviceProperties{ };
    ::vkGetPhysicalDeviceProperties( physical_device, &physical_device_properties );
    spdlog::info(
        "Using Device[{}]: {}",
        physical_device_index,
        physical_device_properties.deviceName
    );

    auto queue_family_count = uint32{ 0 };
    ::vkGetPhysicalDeviceQueueFamilyProperties( physical_device, &queue_family_count, nullptr );
    auto queue_families = std::vector< VkQueueFamilyProperties >( queue_family_count );
    ::vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device,
        &queue_family_count,
        queue_families.data( )
    );

    auto graphics_queue_family_index = std::optional< uint32 >{ };
    auto surface_queue_family_index  = std::optional< uint32 >{ };

    for ( auto i = uint32{ 0 }; i < queue_family_count; ++i )
    {
        if ( ( !graphics_queue_family_index )
             || ( ( nullptr != optional_surface ) && ( !surface_queue_family_index ) ) )
        {
            if ( 0 != ( queue_families[ i ].queueFlags & VK_QUEUE_GRAPHICS_BIT ) )
            {
                graphics_queue_family_index = i;
            }

            if ( nullptr != optional_surface )
            {
                auto surface_support = VkBool32{ false };
                CHECK_VK( ::vkGetPhysicalDeviceSurfaceSupportKHR(
                    physical_device,
                    i,
                    optional_surface,
                    &surface_support
                ) );

                if ( VK_TRUE == surface_support )
                {
                    surface_queue_family_index = i;
                }
            }
        }
    }

    if ( std::nullopt == graphics_queue_family_index )
    {
        spdlog::error( "No graphics queue family found" );
        return false;
    }
    else
    {
        graphics_queue_family_index_out = graphics_queue_family_index.value( );
    }

    if ( ( nullptr != optional_surface ) )
    {
        if ( std::nullopt == surface_queue_family_index )
        {
            spdlog::error( "No surface queue family found" );
            return false;
        }
        else
        {
            *optional_surface_queue_family_index_out = surface_queue_family_index.value( );
        }
    }

    return true;
}

auto initialize_device(
    std::vector< char const* > const& extra_device_extension_names,
    VkPhysicalDevice const&           physical_device,
    uint32                            graphics_queue_family_index,
    VkDevice&                         device,
    VkQueue&                          graphics_queue,
    VkCommandPool&                    graphics_command_pool,
    std::optional< uint32 > const&    optional_surface_queue_family_index,
    VkQueue*                          optional_surface_queue
)
{
    auto unique_queue_indices = std::set{
        graphics_queue_family_index,
    };
    if ( optional_surface_queue_family_index.has_value( ) )
    {
        utils::ignore( unique_queue_indices.insert( optional_surface_queue_family_index.value( ) )
        );
    }

    auto queue_priorities   = std::vector{ 1.0F };
    auto queue_create_infos = std::vector< VkDeviceQueueCreateInfo >{ };

    utils::ignore( std::ranges::transform(
        unique_queue_indices,
        std::back_inserter( queue_create_infos ),
        DeviceQueueCreateInfo{ queue_priorities }
    ) );

    auto device_extension_names = std::vector{
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    };
    utils::ignore( device_extension_names.insert(
        device_extension_names.end( ),
        extra_device_extension_names.begin( ),
        extra_device_extension_names.end( )
    ) );

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
    CHECK_VK( ::vkCreateDevice( physical_device, &device_create_info, nullptr, &device ) );
    spdlog::debug( "vkCreateDevice()" );

    auto constexpr graphics_queue_index = uint32{ 0 };
    ::vkGetDeviceQueue(
        device,
        graphics_queue_family_index,
        graphics_queue_index,
        &graphics_queue
    );

    if ( optional_surface_queue_family_index.has_value( ) )
    {
        auto constexpr surface_queue_index = uint32{ 0 };
        ::vkGetDeviceQueue(
            device,
            optional_surface_queue_family_index.value( ),
            surface_queue_index,
            optional_surface_queue
        );
    }

    auto const command_pool_create_info = VkCommandPoolCreateInfo{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphics_queue_family_index,
    };
    CHECK_VK(
        ::vkCreateCommandPool( device, &command_pool_create_info, nullptr, &graphics_command_pool )
    );
    spdlog::debug( "vkCreateCommandPool()" );

    return true;
}

auto initialize_render_pass(
    VkFormat const      color_format,
    VkImageLayout const final_layout,
    VkDevice const&     device,
    VkRenderPass&       render_pass
)
{
    auto const attachments = std::vector{
        VkAttachmentDescription{
            .flags          = 0U,
            .format         = color_format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = final_layout,
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

    CHECK_VK( ::vkCreateRenderPass( device, &render_pass_create_info, nullptr, &render_pass ) );
    spdlog::debug( "vkCreateRenderPass()" );

    return true;
}

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

template <>
auto initialize( SetupData< AppType::Headless >& setup, uint32 const physical_device_index ) -> bool
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

    auto const extra_extension_names = std::vector< char const* >{ };
    CHECK_TRUE( initialize_instance(
        setup.instance,
        setup.vkDestroyDebugUtilsMessengerEXT,
        setup.debug_messenger,
        extra_extension_names
    ) );

    auto const surface                    = nullptr;
    auto const surface_queue_family_index = nullptr;
    CHECK_TRUE( initialize_physical_device(
        physical_device_index,
        setup.instance,
        setup.physical_device,
        setup.graphics_queue_family_index,
        surface,
        surface_queue_family_index
    ) );

    auto const extra_device_extension_names        = std::vector< char const* >{ };
    auto const optional_surface_queue_family_index = std::optional< uint32 >{ };
    auto const surface_queue                       = nullptr;
    CHECK_TRUE( initialize_device(
        extra_extension_names,
        setup.physical_device,
        setup.graphics_queue_family_index,
        setup.device,
        setup.graphics_queue,
        setup.graphics_command_pool,
        optional_surface_queue_family_index,
        surface_queue
    ) );

    setup.color_format = VK_FORMAT_B8G8R8A8_SRGB;
    CHECK_TRUE( initialize_render_pass(
        setup.color_format,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        setup.device,
        setup.render_pass
    ) );

    return true;
}

template <>
auto initialize( SetupData< AppType::Windowed >& setup, uint32 const physical_device_index ) -> bool
{
    CHECK_TRUE( initialize_glfw( setup.glfw, setup.window ) );

    auto        instance_extension_count = uint32_t{ 0 };
    auto* const glfw_extensions = ::glfwGetRequiredInstanceExtensions( &instance_extension_count );

    auto const extra_extension_names = std::vector< char const* >{
        glfw_extensions,
        glfw_extensions + instance_extension_count,
    };

    CHECK_TRUE( initialize_instance(
        setup.instance,
        setup.vkDestroyDebugUtilsMessengerEXT,
        setup.debug_messenger,
        extra_extension_names
    ) );

    CHECK_VK( ::glfwCreateWindowSurface( setup.instance, setup.window, nullptr, &setup.surface ) );
    spdlog::debug( "glfwCreateWindowSurface()" );

    auto const extra_device_extension_names = std::vector< char const* >{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    CHECK_TRUE( initialize_physical_device(
        physical_device_index,
        setup.instance,
        setup.physical_device,
        setup.graphics_queue_family_index,
        setup.surface,
        &setup.surface_queue_family_index
    ) );

    CHECK_TRUE( initialize_device(
        extra_device_extension_names,
        setup.physical_device,
        setup.graphics_queue_family_index,
        setup.device,
        setup.graphics_queue,
        setup.graphics_command_pool,
        setup.surface_queue_family_index,
        &setup.surface_queue
    ) );

    auto physical_device_formats_count = uint32{ 0 };
    CHECK_VK( ::vkGetPhysicalDeviceSurfaceFormatsKHR(
        setup.physical_device,
        setup.surface,
        &physical_device_formats_count,
        nullptr
    ) );
    auto surface_formats = std::vector< VkSurfaceFormatKHR >( physical_device_formats_count );
    CHECK_VK( ::vkGetPhysicalDeviceSurfaceFormatsKHR(
        setup.physical_device,
        setup.surface,
        &physical_device_formats_count,
        surface_formats.data( )
    ) );

    setup.surface_format = VkSurfaceFormatKHR{
        .format     = VK_FORMAT_B8G8R8A8_SRGB,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    };

    if ( !std::ranges::any_of( surface_formats, SurfaceFormatEquals{ setup.surface_format } ) )
    {
        setup.surface_format = surface_formats[ 0U ];
    }

    CHECK_TRUE( initialize_render_pass(
        setup.surface_format.format,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        setup.device,
        setup.render_pass
    ) );

    return true;
}

template < AppType app_type >
auto destroy( SetupData< app_type >& setup ) -> void
{
    if ( nullptr != setup.render_pass )
    {
        ::vkDestroyRenderPass( setup.device, setup.render_pass, nullptr );
        spdlog::debug( "vkDestroyRenderPass()" );
    }

    if ( nullptr != setup.graphics_command_pool )
    {
        ::vkDestroyCommandPool( setup.device, setup.graphics_command_pool, nullptr );
        spdlog::debug( "vkDestroyCommandPool()" );
    }

    if ( nullptr != setup.device )
    {
        ::vkDestroyDevice( setup.device, nullptr );
        spdlog::debug( "vkDestroyDevice()" );
    }

    if constexpr ( app_type == AppType::Windowed )
    {
        if ( nullptr != setup.surface )
        {
            ::vkDestroySurfaceKHR( setup.instance, setup.surface, nullptr );
            spdlog::debug( "vkDestroySurfaceKHR()" );
        }
    }

    if ( nullptr != setup.debug_messenger )
    {
        setup.vkDestroyDebugUtilsMessengerEXT( setup.instance, setup.debug_messenger, nullptr );
        spdlog::debug( "vkDestroyDebugUtilsMessengerEXT()" );
    }

    if ( nullptr != setup.instance )
    {
        ::vkDestroyInstance( setup.instance, nullptr );
        spdlog::debug( "vkDestroyInstance()" );
    }

    if constexpr ( app_type == AppType::Windowed )
    {
        if ( nullptr != setup.window )
        {
            ::glfwDestroyWindow( setup.window );
            spdlog::debug( "glfwDestroyWindow()" );
        }

        if ( GLFW_TRUE == setup.glfw )
        {
            ::glfwTerminate( );
            spdlog::debug( "glfwTerminate()" );
        }
    }
}

template auto destroy( SetupData< AppType::Headless >& setup ) -> void;
template auto destroy( SetupData< AppType::Windowed >& setup ) -> void;

} // namespace ltb::vlk
