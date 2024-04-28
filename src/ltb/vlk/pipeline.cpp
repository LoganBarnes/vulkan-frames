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

template < Pipeline pipeline_type >
auto initialize(
    PipelineData< pipeline_type >& pipeline,
    VkDevice const&                device,
    VkRenderPass const&            render_pass,
    uint32 const                   max_frames_in_flight
) -> bool
{
    auto descriptor_set_layouts = std::vector< VkDescriptorSetLayout >{ };
    auto push_constant_ranges   = std::vector< VkPushConstantRange >{ };

    if constexpr ( pipeline_type == Pipeline::Triangle )
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
        auto const descriptor_pool_sizes = std::array{
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
            device,
            &descriptor_pool_create_info,
            nullptr,
            &pipeline.descriptor_pool
        ) );
        spdlog::debug( "vkCreateDescriptorPool()" );

        auto const descriptor_set_layout_bindings = std::array{
            VkDescriptorSetLayoutBinding{
                .binding            = 0U,
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
            device,
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

        pipeline.descriptor_sets.resize( max_frames_in_flight );
        CHECK_VK( ::vkAllocateDescriptorSets(
            device,
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
        device,
        &pipeline_layout_info,
        nullptr,
        &pipeline.pipeline_layout
    ) );
    spdlog::debug( "vkCreatePipelineLayout()" );

    auto vert_shader_path = config::spirv_shader_dir_path( );
    auto frag_shader_path = config::spirv_shader_dir_path( );
    auto topology         = VkPrimitiveTopology{ };

    if constexpr ( pipeline_type == Pipeline::Triangle )
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
        .flags    = 0U,
        .codeSize = vert_shader_code.size( ) * sizeof( uint32_t ),
        .pCode    = vert_shader_code.data( ),
    };
    auto* vert_shader_module = VkShaderModule{ };
    CHECK_VK( ::vkCreateShaderModule(
        device,
        &vert_shader_module_create_info,
        nullptr,
        &vert_shader_module
    ) );

    auto const frag_shader_module_create_info = VkShaderModuleCreateInfo{
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext    = nullptr,
        .flags    = 0U,
        .codeSize = frag_shader_code.size( ) * sizeof( uint32_t ),
        .pCode    = frag_shader_code.data( ),
    };
    auto* frag_shader_module = VkShaderModule{ };
    CHECK_VK( ::vkCreateShaderModule(
        device,
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
        .renderPass          = render_pass,
        .subpass             = 0U,
        .basePipelineHandle  = nullptr,
        .basePipelineIndex   = -1,
    };
    CHECK_VK( ::vkCreateGraphicsPipelines(
        device,
        nullptr,
        1,
        &pipeline_create_info,
        nullptr,
        &pipeline.pipeline
    ) );
    spdlog::debug( "vkCreateGraphicsPipelines()" );

    ::vkDestroyShaderModule( device, frag_shader_module, nullptr );
    ::vkDestroyShaderModule( device, vert_shader_module, nullptr );

    return true;
}

template auto initialize(
    PipelineData< Pipeline::Triangle >&,
    VkDevice const&,
    VkRenderPass const&,
    uint32 const
) -> bool;

template auto initialize(
    PipelineData< Pipeline::Composite >&,
    VkDevice const&,
    VkRenderPass const&,
    uint32 const
) -> bool;

template < Pipeline pipeline_type >
auto destroy( PipelineData< pipeline_type >& pipeline, VkDevice const& device ) -> void
{
    if ( nullptr != pipeline.pipeline )
    {
        ::vkDestroyPipeline( device, pipeline.pipeline, nullptr );
        spdlog::debug( "vkDestroyPipeline()" );
    }

    if ( nullptr != pipeline.pipeline_layout )
    {
        ::vkDestroyPipelineLayout( device, pipeline.pipeline_layout, nullptr );
        spdlog::debug( "vkDestroyPipelineLayout()" );
    }

    if constexpr ( Pipeline::Composite == pipeline_type )
    {
        if ( nullptr != pipeline.descriptor_set_layout )
        {
            ::vkDestroyDescriptorSetLayout( device, pipeline.descriptor_set_layout, nullptr );
            spdlog::debug( "vkDestroyDescriptorSetLayout()" );
        }

        if ( nullptr != pipeline.descriptor_pool )
        {
            ::vkDestroyDescriptorPool( device, pipeline.descriptor_pool, nullptr );
            spdlog::debug( "vkDestroyDescriptorPool()" );
        }
    }
}

template auto destroy( PipelineData< Pipeline::Triangle >&, VkDevice const& ) -> void;
template auto destroy( PipelineData< Pipeline::Composite >&, VkDevice const& ) -> void;

} // namespace ltb::vlk
