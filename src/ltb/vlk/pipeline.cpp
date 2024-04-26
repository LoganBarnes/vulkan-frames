// /////////////////////////////////////////////////////////////
// A Logan Thomas Barnes project
// /////////////////////////////////////////////////////////////
#include "ltb/vlk/pipeline.hpp"

// project
#include "ltb/ltb_config.hpp"
#include "ltb/utils/read_file.hpp"
#include "ltb/vlk/check.hpp"

namespace ltb::vlk
{
namespace
{

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

} // namespace

template < AppType app_type, Pipeline pipeline_type >
auto initialize(
    SetupData< app_type > const&   setup,
    PipelineData< pipeline_type >& pipeline,
    uint32 const                   max_frames_in_flight
) -> bool
{
    if constexpr ( app_type == AppType::Headless )
    {
        CHECK_TRUE( initialize_render_pass(
            setup.color_format,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            setup.device,
            pipeline.render_pass
        ) );
    }
    else
    {
        CHECK_TRUE( initialize_render_pass(
            setup.surface_format.format,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            setup.device,
            pipeline.render_pass
        ) );
    }

    auto descriptor_set_layouts = std::vector< VkDescriptorSetLayout >{ };
    auto push_constant_ranges   = std::vector< VkPushConstantRange >{ };

    if constexpr ( Pipeline::Triangle == pipeline_type )
    {
        push_constant_ranges = {
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset     = 0,
                .size       = sizeof( pipeline.model_uniforms ),
            },
            {
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset     = sizeof( pipeline.model_uniforms ),
                .size       = sizeof( pipeline.display_uniforms ),
            },
        };
    }
    else
    {
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
            setup.device,
            &descriptor_pool_create_info,
            nullptr,
            &pipeline.descriptor_pool
        ) );
        spdlog::debug( "vkCreateDescriptorPool()" );

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
            setup.device,
            &descriptor_set_layout_info,
            nullptr,
            &pipeline.descriptor_set_layout
        ) );

        auto const layouts = std::vector<
            VkDescriptorSetLayout >( max_frames_in_flight, pipeline.descriptor_set_layout );
        auto const descriptor_set_allocate_info = VkDescriptorSetAllocateInfo{
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext              = nullptr,
            .descriptorPool     = pipeline.descriptor_pool,
            .descriptorSetCount = max_frames_in_flight,
            .pSetLayouts        = layouts.data( ),
        };

        pipeline.descriptor_sets.resize( descriptor_set_allocate_info.descriptorSetCount );
        CHECK_VK( ::vkAllocateDescriptorSets(
            setup.device,
            &descriptor_set_allocate_info,
            pipeline.descriptor_sets.data( )
        ) );

        descriptor_set_layouts = { pipeline.descriptor_set_layout };
    }

    auto const pipeline_layout_info = VkPipelineLayoutCreateInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0U,
        .setLayoutCount         = static_cast< uint32 >( descriptor_set_layouts.size( ) ),
        .pSetLayouts            = descriptor_set_layouts.data( ),
        .pushConstantRangeCount = static_cast< uint32 >( push_constant_ranges.size( ) ),
        .pPushConstantRanges    = push_constant_ranges.data( ),
    };
    CHECK_VK( ::vkCreatePipelineLayout(
        setup.device,
        &pipeline_layout_info,
        nullptr,
        &pipeline.pipeline_layout
    ) );
    spdlog::debug( "vkCreatePipelineLayout()" );

    auto vert_shader_path = config::spirv_shader_dir_path( );
    auto frag_shader_path = config::spirv_shader_dir_path( );
    auto topology         = VkPrimitiveTopology{ };

    if constexpr ( Pipeline::Triangle == pipeline_type )
    {
        vert_shader_path = vert_shader_path / "triangle.vert.spv";
        frag_shader_path = frag_shader_path / "triangle.frag.spv";
        topology         = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
    else
    {
        vert_shader_path = vert_shader_path / "composite.vert.spv";
        frag_shader_path = frag_shader_path / "composite.frag.spv";
        topology         = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    }

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
        setup.device,
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
        setup.device,
        &frag_shader_module_create_info,
        nullptr,
        &frag_shader_module
    ) );

    auto const shader_stages = std::vector{
        VkPipelineShaderStageCreateInfo{
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0U,
            .stage               = VK_SHADER_STAGE_VERTEX_BIT,
            .module              = vert_shader_module,
            .pName               = "main",
            .pSpecializationInfo = nullptr,
        },
        VkPipelineShaderStageCreateInfo{
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0U,
            .stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module              = frag_shader_module,
            .pName               = "main",
            .pSpecializationInfo = nullptr,
        },
    };

    auto const vertex_input_info = VkPipelineVertexInputStateCreateInfo{
        .sType                         = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext                         = nullptr,
        .flags                         = 0U,
        .vertexBindingDescriptionCount = 0U,
        .pVertexBindingDescriptions    = nullptr,
        .vertexAttributeDescriptionCount = 0U,
        .pVertexAttributeDescriptions    = nullptr,
    };

    auto const input_assembly = VkPipelineInputAssemblyStateCreateInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0U,
        .topology               = topology,
        .primitiveRestartEnable = VK_FALSE,
    };

    auto const viewport_state = VkPipelineViewportStateCreateInfo{
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = 0U,
        .viewportCount = 1U,
        .pViewports    = nullptr,
        .scissorCount  = 1U,
        .pScissors     = nullptr,
    };

    auto const rasterizer = VkPipelineRasterizationStateCreateInfo{
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0U,
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
        .flags           = 0U,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = 1U,
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
        .layout              = pipeline.pipeline_layout,
        .renderPass          = pipeline.render_pass,
        .subpass             = 0U,
        .basePipelineHandle  = nullptr,
        .basePipelineIndex   = -1,
    };
    CHECK_VK( ::vkCreateGraphicsPipelines(
        setup.device,
        nullptr,
        1,
        &pipeline_create_info,
        nullptr,
        &pipeline.pipeline
    ) );
    spdlog::debug( "vkCreateGraphicsPipelines()" );

    ::vkDestroyShaderModule( setup.device, frag_shader_module, nullptr );
    ::vkDestroyShaderModule( setup.device, vert_shader_module, nullptr );

    return true;
}

template auto
initialize( SetupData< AppType::Headless > const&, PipelineData< Pipeline::Triangle >&, uint32 )
    -> bool;

template auto
initialize( SetupData< AppType::Windowed > const&, PipelineData< Pipeline::Triangle >&, uint32 )
    -> bool;

template auto
initialize( SetupData< AppType::Headless > const&, PipelineData< Pipeline::Composite >&, uint32 )
    -> bool;

template auto
initialize( SetupData< AppType::Windowed > const&, PipelineData< Pipeline::Composite >&, uint32 )
    -> bool;

} // namespace ltb::vlk
