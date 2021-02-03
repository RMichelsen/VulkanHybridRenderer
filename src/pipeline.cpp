#include "pch.h"
#include "pipeline.h"

#include "resource_manager.h"
#include "vulkan_context.h"
#include "vulkan_pipeline_presets.h"
#include "vulkan_utils.h"

namespace VkUtils {
GraphicsPipeline CreateGraphicsPipeline(VulkanContext &context, ResourceManager &resource_manager, 
	RenderPass render_pass, GraphicsPipelineDescription description) {
	GraphicsPipeline pipeline {
		.description = description
	};

	std::vector<uint32_t> vertex_bytecode =
		VkUtils::LoadShader(description.vertex_shader, VK_SHADER_STAGE_VERTEX_BIT);
	std::vector<uint32_t> fragment_bytecode =
		VkUtils::LoadShader(description.fragment_shader, VK_SHADER_STAGE_FRAGMENT_BIT);

	VkShaderModule vertex_shader;
	VkShaderModuleCreateInfo vertex_shader_module_info {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = vertex_bytecode.size() * sizeof(uint32_t),
		.pCode = vertex_bytecode.data()
	};
	VK_CHECK(vkCreateShaderModule(context.device, &vertex_shader_module_info,
		nullptr, &vertex_shader));

	VkShaderModule fragment_shader;
	VkShaderModuleCreateInfo fragment_shader_module_info {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = fragment_bytecode.size() * sizeof(uint32_t),
		.pCode = fragment_bytecode.data()
	};
	VK_CHECK(vkCreateShaderModule(context.device, &fragment_shader_module_info, \
		nullptr, &fragment_shader));

	std::array<VkPipelineShaderStageCreateInfo, 2> shader_stage_infos {
		VkPipelineShaderStageCreateInfo {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex_shader,
			.pName = "main",
		},
		VkPipelineShaderStageCreateInfo {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment_shader,
			.pName = "main",
		}
	};

	VkPipelineVertexInputStateCreateInfo vertex_input_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};
	if(description.vertex_input_state == VertexInputState::Default) {
		vertex_input_state_info.vertexBindingDescriptionCount = 1;
		vertex_input_state_info.pVertexBindingDescriptions = &VERTEX_BINDING_DESCRIPTION;
		vertex_input_state_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(VERTEX_ATTRIBUTE_DESCRIPTIONS.size());
		vertex_input_state_info.pVertexAttributeDescriptions = VERTEX_ATTRIBUTE_DESCRIPTIONS.data();
	}
	VkPipelineInputAssemblyStateCreateInfo input_assembly_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};
	VkViewport viewport {
		.y = static_cast<float>(context.swapchain.extent.height),
		.width = static_cast<float>(context.swapchain.extent.width),
		.height = -static_cast<float>(context.swapchain.extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	VkRect2D scissor {
		.extent = context.swapchain.extent
	};
	VkPipelineViewportStateCreateInfo viewport_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &viewport,
		.scissorCount = 1,
		.pScissors = &scissor
	};

	std::vector<VkPushConstantRange> push_constants;
	if(description.push_constants.size > 0) {
		push_constants.emplace_back(
			VkPushConstantRange {
				.stageFlags = description.push_constants.pipeline_stages,
				.offset = 0,
				.size = description.push_constants.size
			}
		);
	}

	std::vector<VkDescriptorSetLayout> descriptor_set_layouts { 
		resource_manager.global_descriptor_set_layout,
		resource_manager.per_frame_descriptor_set_layout,
	};
	if(render_pass.descriptor_set_layout != VK_NULL_HANDLE) {
		descriptor_set_layouts.emplace_back(render_pass.descriptor_set_layout);
	}
	VkPipelineLayoutCreateInfo layout_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = static_cast<uint32_t>(descriptor_set_layouts.size()),
		.pSetLayouts = descriptor_set_layouts.data(),
		.pushConstantRangeCount = static_cast<uint32_t>(push_constants.size()),
		.pPushConstantRanges = push_constants.empty() ? nullptr : push_constants.data()
	};

	VK_CHECK(vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline.layout));

	VkGraphicsPipelineCreateInfo pipeline_info {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = static_cast<uint32_t>(shader_stage_infos.size()),
		.pStages = shader_stage_infos.data(),
		.pVertexInputState = &vertex_input_state_info,
		.pInputAssemblyState = &input_assembly_state_info,
		.pViewportState = &viewport_state_info,
		.layout = pipeline.layout,
		.renderPass = render_pass.handle,
		.subpass = 0
	};

	switch(description.rasterization_state) {
	case RasterizationState::Fill:
		pipeline_info.pRasterizationState = &RASTERIZATION_STATE_FILL; break;
	case RasterizationState::Wireframe:
		pipeline_info.pRasterizationState = &RASTERIZATION_STATE_WIREFRAME; break;
	case RasterizationState::FillCullCCW:
		pipeline_info.pRasterizationState = &RASTERIZATION_STATE_FILL_CULL_CCW; break;
	}
	switch(description.multisample_state) {
	case MultisampleState::Off:
		pipeline_info.pMultisampleState = &MULTISAMPLE_STATE_OFF; break;
	}
	switch(description.depth_stencil_state) {
	case DepthStencilState::On:
		pipeline_info.pDepthStencilState = &DEPTH_STENCIL_STATE_ON; break;
	case DepthStencilState::Off:
		pipeline_info.pDepthStencilState = &DEPTH_STENCIL_STATE_OFF; break;
	}

	std::vector<VkPipelineColorBlendAttachmentState> color_blend_states;
	for(TransientResource &resource : render_pass.attachments) {
		if(VkUtils::IsDepthFormat(resource.texture.format)) {
			continue;
		}
		if(!resource.texture.color_blending) {
			color_blend_states.emplace_back(COLOR_BLEND_ATTACHMENT_STATE_OFF);
		}
	}
	VkPipelineColorBlendStateCreateInfo color_blend_state {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = static_cast<uint32_t>(color_blend_states.size()),
		.pAttachments = color_blend_states.data()
	};
	pipeline_info.pColorBlendState = &color_blend_state;

	VK_CHECK(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
		nullptr, &pipeline.handle));

	return pipeline;
}

RaytracingPipeline CreateRaytracingPipeline(VulkanContext &context, ResourceManager &resource_manager,
	RaytracingPipelineDescription description) {
	RaytracingPipeline pipeline {};

	return pipeline;
}
}