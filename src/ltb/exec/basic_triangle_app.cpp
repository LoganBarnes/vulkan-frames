// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////

// project
#include "ltb/ltb_config.hpp"
#include "ltb/utils/ignore.hpp"
#include "ltb/utils/read_file.hpp"
#include "ltb/vlk/check.hpp"
#include "ltb/vlk/setup.hpp"

// external
#include <spdlog/spdlog.h>

// standard
#include <algorithm>
#include <charconv>
#include <optional>
#include <ranges>
#include <set>
#include <vector>

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
    // Common setup objects
    vlk::SetupData< vlk::AppType::Windowed > setup_ = { };

    // Pools
    VkDescriptorPool descriptor_pool_ = { };

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
    ModelUniforms    model_uniforms_   = { };
    DisplayUniforms  display_uniforms_ = { };
    VkPipelineLayout pipeline_layout_  = { };
    VkPipeline       pipeline_         = { };
};

App::~App( )
{
    if ( nullptr != pipeline_ )
    {
        ::vkDestroyPipeline( setup_.device, pipeline_, nullptr );
        spdlog::debug( "vkDestroyPipeline()" );
    }

    if ( nullptr != pipeline_layout_ )
    {
        ::vkDestroyPipelineLayout( setup_.device, pipeline_layout_, nullptr );
        spdlog::debug( "vkDestroyPipelineLayout()" );
    }

    for ( auto* const framebuffer : framebuffers_ )
    {
        ::vkDestroyFramebuffer( setup_.device, framebuffer, nullptr );
    }
    spdlog::debug( "vkDestroyFramebuffer()x{}", framebuffers_.size( ) );
    framebuffers_.clear( );

    for ( auto* const image_view : swapchain_image_views_ )
    {
        ::vkDestroyImageView( setup_.device, image_view, nullptr );
    }
    spdlog::debug( "vkDestroyImageView()x{}", swapchain_image_views_.size( ) );
    swapchain_image_views_.clear( );

    swapchain_images_.clear( );
    if ( nullptr != swapchain_ )
    {
        ::vkDestroySwapchainKHR( setup_.device, swapchain_, nullptr );
        spdlog::debug( "vkDestroySwapchainKHR()" );
    }

    for ( auto* const fence : graphics_queue_fences_ )
    {
        ::vkDestroyFence( setup_.device, fence, nullptr );
    }
    spdlog::debug( "vkDestroyFence()x{}", graphics_queue_fences_.size( ) );
    graphics_queue_fences_.clear( );

    for ( auto* const semaphore : render_finished_semaphores_ )
    {
        ::vkDestroySemaphore( setup_.device, semaphore, nullptr );
    }
    spdlog::debug( "vkDestroySemaphore()x{}", render_finished_semaphores_.size( ) );
    render_finished_semaphores_.clear( );

    for ( auto* const semaphore : image_available_semaphores_ )
    {
        ::vkDestroySemaphore( setup_.device, semaphore, nullptr );
    }
    spdlog::debug( "vkDestroySemaphore()x{}", image_available_semaphores_.size( ) );
    image_available_semaphores_.clear( );

    if ( !command_buffers_.empty( ) )
    {
        ::vkFreeCommandBuffers(
            setup_.device,
            setup_.graphics_command_pool,
            static_cast< uint32 >( command_buffers_.size( ) ),
            command_buffers_.data( )
        );
        spdlog::debug( "vkFreeCommandBuffers()" );
    }

    if ( nullptr != descriptor_pool_ )
    {
        ::vkDestroyDescriptorPool( setup_.device, descriptor_pool_, nullptr );
        spdlog::debug( "vkDestroyDescriptorPool()" );
    }

    vlk::destroy( setup_ );
}

auto App::initialize( uint32 const physical_device_index ) -> bool
{
    CHECK_TRUE( vlk::initialize( setup_, physical_device_index ) );

    auto const cmd_buf_alloc_info = VkCommandBufferAllocateInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = setup_.graphics_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = max_frames_in_flight,
    };
    command_buffers_.resize( cmd_buf_alloc_info.commandBufferCount );
    CHECK_VK(
        ::vkAllocateCommandBuffers( setup_.device, &cmd_buf_alloc_info, command_buffers_.data( ) )
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
            setup_.device,
            &semaphore_create_info,
            nullptr,
            image_available_semaphores_.data( ) + i
        ) );
        CHECK_VK( ::vkCreateSemaphore(
            setup_.device,
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
            setup_.device,
            &fence_create_info,
            nullptr,
            graphics_queue_fences_.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateFence()x{}", max_frames_in_flight );

    auto framebuffer_width  = int32{ 0 };
    auto framebuffer_height = int32{ 0 };
    ::glfwGetFramebufferSize( setup_.window, &framebuffer_width, &framebuffer_height );

    auto surface_capabilities = VkSurfaceCapabilitiesKHR{ };
    CHECK_VK( ::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        setup_.physical_device,
        setup_.surface,
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

    auto const unique_queue_indices = std::set{
        setup_.graphics_queue_family_index,
        setup_.surface_queue_family_index,
    };

    auto const queue_family_indices
        = std::vector< uint32 >( unique_queue_indices.begin( ), unique_queue_indices.end( ) );

    auto const concurrency               = ( queue_family_indices.size( ) > 1 );
    auto const unique_queue_family_count = static_cast< uint32 >( queue_family_indices.size( ) );

    auto const swapchain_create_info = VkSwapchainCreateInfoKHR{
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext            = nullptr,
        .flags            = 0U,
        .surface          = setup_.surface,
        .minImageCount    = min_image_count,
        .imageFormat      = setup_.surface_format.format,
        .imageColorSpace  = setup_.surface_format.colorSpace,
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

    CHECK_VK( ::vkCreateSwapchainKHR( setup_.device, &swapchain_create_info, nullptr, &swapchain_ )
    );
    spdlog::debug( "vkCreateSwapchainKHR()" );

    auto swapchain_image_count = uint32{ 0 };
    CHECK_VK(
        ::vkGetSwapchainImagesKHR( setup_.device, swapchain_, &swapchain_image_count, nullptr )
    );
    swapchain_images_.resize( swapchain_image_count );
    CHECK_VK( ::vkGetSwapchainImagesKHR(
        setup_.device,
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
            .format           = setup_.surface_format.format,
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
            setup_.device,
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
            .renderPass      = setup_.render_pass,
            .attachmentCount = 1U,
            .pAttachments    = swapchain_image_views_.data( ) + i,
            .width           = framebuffer_size_.width,
            .height          = framebuffer_size_.height,
            .layers          = 1U,
        };
        CHECK_VK( ::vkCreateFramebuffer(
            setup_.device,
            &framebuffer_create_info,
            nullptr,
            framebuffers_.data( ) + i
        ) );
    }
    spdlog::debug( "vkCreateFramebuffer()x{}", framebuffers_.size( ) );

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
        .flags                  = 0U,
        .setLayoutCount         = 0U,
        .pSetLayouts            = nullptr,
        .pushConstantRangeCount = static_cast< uint32 >( push_constant_ranges.size( ) ),
        .pPushConstantRanges    = push_constant_ranges.data( ),
    };
    CHECK_VK(
        ::vkCreatePipelineLayout( setup_.device, &pipeline_layout_info, nullptr, &pipeline_layout_ )
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
        setup_.device,
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
        setup_.device,
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
        .renderPass          = setup_.render_pass,
        .subpass             = 0,
        .basePipelineHandle  = nullptr,
        .basePipelineIndex   = -1,
    };
    CHECK_VK( ::vkCreateGraphicsPipelines(
        setup_.device,
        nullptr,
        1,
        &pipeline_create_info,
        nullptr,
        &pipeline_
    ) );
    spdlog::debug( "vkCreateGraphicsPipelines()" );

    ::vkDestroyShaderModule( setup_.device, frag_shader_module, nullptr );
    ::vkDestroyShaderModule( setup_.device, vert_shader_module, nullptr );

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

        model_uniforms_.scale_rotation_translation[ 1 ]
            = M_PI_2f * angular_velocity_rps * current_duration_s;

        // Render pipeline here.
        auto* const graphics_queue_fence = graphics_queue_fences_[ current_frame_ ];

        auto const graphics_fences = std::array{ graphics_queue_fence };

        CHECK_VK( ::vkWaitForFences(
            setup_.device,
            static_cast< uint32 >( graphics_fences.size( ) ),
            graphics_fences.data( ),
            VK_TRUE,
            max_possible_timeout
        ) );

        auto* const image_available_semaphore = image_available_semaphores_[ current_frame_ ];

        auto swapchain_image_index = uint32{ 0 };
        CHECK_VK( ::vkAcquireNextImageKHR(
            setup_.device,
            swapchain_,
            max_possible_timeout,
            image_available_semaphore,
            nullptr,
            &swapchain_image_index
        ) );
        auto* const framebuffer = framebuffers_[ swapchain_image_index ];

        CHECK_VK( ::vkResetFences(
            setup_.device,
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
            .renderPass      = setup_.render_pass,
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

        ::vkCmdPushConstants(
            command_buffer,
            pipeline_layout_,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof( model_uniforms_ ),
            &model_uniforms_
        );

        ::vkCmdPushConstants(
            command_buffer,
            pipeline_layout_,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            sizeof( model_uniforms_ ),
            sizeof( display_uniforms_ ),
            &display_uniforms_
        );

        auto constexpr vertex_count   = 3U;
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
        CHECK_VK( ::vkQueueSubmit(
            setup_.graphics_queue,
            submit_count,
            &submit_info,
            graphics_queue_fence
        ) );

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
        CHECK_VK( ::vkQueuePresentKHR( setup_.surface_queue, &present_info ) );

        current_frame_ = ( current_frame_ + 1U ) % max_frames_in_flight;

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
