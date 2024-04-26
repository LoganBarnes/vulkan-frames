// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////

// project
#include "ltb/ltb_config.hpp"
#include "ltb/net/fd_socket.hpp"
#include "ltb/utils/read_file.hpp"
#include "ltb/vlk/check.hpp"
#include "ltb/vlk/output.hpp"

// external
#include <spdlog/spdlog.h>

// standard
#include <cerrno>
#include <charconv>
#include <filesystem>
#include <optional>
#include <ranges>
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
    // Initialization
    vlk::SetupData< vlk::AppType::Headless > setup_ = { };

    // Synchronization objects
    VkCommandBuffer command_buffer_       = { };
    VkFence         graphics_queue_fence_ = { };

    // Output
    vlk::OutputData< vlk::AppType::Headless > output_ = { };

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
        ::vkDestroyPipeline( setup_.device, pipeline_, nullptr );
        spdlog::debug( "vkDestroyPipeline()" );
    }

    if ( nullptr != pipeline_layout_ )
    {
        ::vkDestroyPipelineLayout( setup_.device, pipeline_layout_, nullptr );
        spdlog::debug( "vkDestroyPipelineLayout()" );
    }

    vlk::destroy( setup_, output_ );

    if ( nullptr != graphics_queue_fence_ )
    {
        ::vkDestroyFence( setup_.device, graphics_queue_fence_, nullptr );
        spdlog::debug( "vkDestroyFence()" );
    }

    if ( nullptr != command_buffer_ )
    {
        ::vkFreeCommandBuffers( setup_.device, setup_.graphics_command_pool, 1, &command_buffer_ );
        spdlog::debug( "vkFreeCommandBuffers()" );
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
        .commandBufferCount = 1,
    };

    CHECK_VK( ::vkAllocateCommandBuffers( setup_.device, &cmd_buf_alloc_info, &command_buffer_ ) );
    spdlog::debug( "vkAllocateCommandBuffers()" );

    auto const fence_create_info = VkFenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    CHECK_VK( ::vkCreateFence( setup_.device, &fence_create_info, nullptr, &graphics_queue_fence_ )
    );
    spdlog::debug( "vkCreateFence()" );

    CHECK_TRUE( vlk::initialize( image_extents, vlk::ExternalMemory::Yes, setup_, output_ ) );

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
             = ::read( STDIN_FILENO, setup_.input_buffer.data( ), setup_.input_buffer.size( ) );
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
            setup_.device,
            static_cast< uint32 >( graphics_fences.size( ) ),
            graphics_fences.data( ),
            VK_TRUE,
            max_possible_timeout
        ) );

        CHECK_VK( ::vkResetFences(
            setup_.device,
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
            .renderPass      = setup_.render_pass,
            .framebuffer     = output_.framebuffer,
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
        CHECK_VK( ::vkQueueSubmit(
            setup_.graphics_queue,
            submit_count,
            &submit_info,
            graphics_queue_fence_
        ) );

        if ( -1 == color_image_fd_ )
        {
            CHECK_VK( ::vkDeviceWaitIdle( setup_.device ) );

            auto* const vkGetMemoryFdKHR = reinterpret_cast< PFN_vkGetMemoryFdKHR >(
                ::vkGetInstanceProcAddr( setup_.instance, "vkGetMemoryFdKHR" )
            );
            if ( nullptr == vkGetMemoryFdKHR )
            {
                spdlog::error( "vkGetInstanceProcAddr() failed" );
                return false;
            }

            auto const memory_info = VkMemoryGetFdInfoKHR{
                .sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
                .pNext      = nullptr,
                .memory     = output_.color_image_memory,
                .handleType = external_memory_handle_type,
            };
            CHECK_VK( vkGetMemoryFdKHR( setup_.device, &memory_info, &color_image_fd_ ) );
            spdlog::debug( "vkGetMemoryFdKHR()" );

            spdlog::info( "Color image file descriptor: {}", color_image_fd_ );

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

    CHECK_VK( ::vkDeviceWaitIdle( setup_.device ) );
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
