#include "pch.h"
#include "pipeline.h"

#include "resource_manager.h"
#include "shader_compiler.h"
#include "vulkan_context.h"
#include "vulkan_pipeline_presets.h"

namespace VkUtils {
GraphicsPipeline CreateGraphicsPipeline(VulkanContext &context, ResourceManager &resource_manager, 
	RenderPass render_pass, GraphicsPipelineDescription description) {
	GraphicsPipeline pipeline;

	std::vector<uint32_t> vertex_bytecode =
		VkUtils::CompileShader(description.vertex_shader, VK_SHADER_STAGE_VERTEX_BIT);
	std::vector<uint32_t> fragment_bytecode =
		VkUtils::CompileShader(description.fragment_shader, VK_SHADER_STAGE_FRAGMENT_BIT);

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
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &VERTEX_BINDING_DESCRIPTION,
		.vertexAttributeDescriptionCount = static_cast<uint32_t>(VERTEX_ATTRIBUTE_DESCRIPTIONS.size()),
		.pVertexAttributeDescriptions = VERTEX_ATTRIBUTE_DESCRIPTIONS.data()
	};
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
	VkPushConstantRange push_constant_range {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(PushConstants)
	};

	std::vector<VkDescriptorSetLayout> descriptor_set_layouts { 
		resource_manager.texture_array_descriptor_set_layout,
	};
	if(render_pass.descriptor_set_layout != VK_NULL_HANDLE) {
		descriptor_set_layouts.emplace_back(render_pass.descriptor_set_layout);
	}
	VkPipelineLayoutCreateInfo layout_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = static_cast<uint32_t>(descriptor_set_layouts.size()),
		.pSetLayouts = descriptor_set_layouts.data(),
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_constant_range
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
	for(ColorBlendState color_blend_state : description.color_blend_states) {
		if(color_blend_state == ColorBlendState::Off) {
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