#include "pch.h"
#include "pipeline.h"

#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_pipeline_presets.h"
#include "rendering_backend/vulkan_utils.h"

namespace VkUtils {
GraphicsPipeline CreateGraphicsPipeline(VulkanContext &context, ResourceManager &resource_manager, 
	RenderPass &render_pass, GraphicsPipelineDescription description) {
	assert(std::holds_alternative<GraphicsPass>(render_pass.pass));
	GraphicsPass &graphics_pass = std::get<GraphicsPass>(render_pass.pass);
	GraphicsPipeline pipeline {
		.description = description
	};

	std::array<VkPipelineShaderStageCreateInfo, 2> shader_stage_infos {
		VkUtils::PipelineShaderStageCreateInfo(context.device, description.vertex_shader,
			VK_SHADER_STAGE_VERTEX_BIT),
		VkUtils::PipelineShaderStageCreateInfo(context.device, description.fragment_shader,
			VK_SHADER_STAGE_FRAGMENT_BIT)
	};

	uint32_t num_spec_constants = 
		static_cast<uint32_t>(description.specialization_constants_description.specialization_constants.size());
	std::vector<VkSpecializationMapEntry> specialization_map_entries;
	VkSpecializationInfo specialization_info;
	if(num_spec_constants > 0) {
		specialization_map_entries = VkUtils::CreateSpecializationMapEntries(num_spec_constants);

		specialization_info.mapEntryCount = static_cast<uint32_t>(specialization_map_entries.size());
		specialization_info.pMapEntries = specialization_map_entries.data();
		specialization_info.dataSize = sizeof(int) * num_spec_constants;
		specialization_info.pData = description.specialization_constants_description.specialization_constants.data();

		if(description.specialization_constants_description.shader_stage & VK_SHADER_STAGE_VERTEX_BIT) {
			shader_stage_infos[0].pSpecializationInfo = &specialization_info;
		}
		if(description.specialization_constants_description.shader_stage & VK_SHADER_STAGE_FRAGMENT_BIT) {
			shader_stage_infos[1].pSpecializationInfo = &specialization_info;
		}
	}

	VkPipelineVertexInputStateCreateInfo vertex_input_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
	};
	if(description.vertex_input_state == VertexInputState::Default) {
		vertex_input_state_info = VERTEX_INPUT_STATE_DEFAULT;
	}
	else if(description.vertex_input_state == VertexInputState::ImGui) {
		vertex_input_state_info = VERTEX_INPUT_STATE_IMGUI;
	}
	VkPipelineInputAssemblyStateCreateInfo input_assembly_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};

	std::vector<VkPushConstantRange> push_constants;
	if(description.push_constants.size > 0) {
		push_constants.emplace_back(
			VkPushConstantRange {
				.stageFlags = description.push_constants.pipeline_stage,
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
		.layout = pipeline.layout,
		.renderPass = graphics_pass.handle,
		.subpass = 0
	};

	VkPipelineRasterizationStateCreateInfo rasterization_state = RASTERIZATION_STATE_DEFAULT;
	pipeline_info.pRasterizationState = &rasterization_state;
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

	switch(description.dynamic_state) {
	case DynamicState::ViewportScissor:
		pipeline_info.pDynamicState = &DYNAMIC_STATE_VIEWPORT_SCISSOR; break;
	case DynamicState::DepthBias: {
		pipeline_info.pDynamicState = &DYNAMIC_STATE_DEPTH_BIAS;
		rasterization_state = *pipeline_info.pRasterizationState;
		rasterization_state.depthBiasEnable = VK_TRUE;
		pipeline_info.pRasterizationState = &rasterization_state;
	} break;
	case DynamicState::None: break;
	}

	bool contains_render_output = false;
	assert(!graphics_pass.attachments.empty());
	uint32_t pass_width = graphics_pass.attachments[0].image.width;
	uint32_t pass_height = graphics_pass.attachments[0].image.height;
	std::vector<VkPipelineColorBlendAttachmentState> color_blend_states;
	for(TransientResource &attachment : graphics_pass.attachments) {
		if(!strcmp(attachment.name, "RENDER_OUTPUT")) {
			contains_render_output = true;
		}
		assert(attachment.image.width == pass_width);
		assert(attachment.image.height == pass_height);
		if(VkUtils::IsDepthFormat(attachment.image.format)) {
			continue;
		}
		switch(attachment.image.color_blend_state) {
		case ColorBlendState::Off: {
			color_blend_states.emplace_back(COLOR_BLEND_ATTACHMENT_STATE_OFF);
		} break;
		case ColorBlendState::ImGui: {
			color_blend_states.emplace_back(COLOR_BLEND_ATTACHMENT_STATE_IMGUI);
		} break;
		}
	}
	VkPipelineColorBlendStateCreateInfo color_blend_state {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = static_cast<uint32_t>(color_blend_states.size()),
		.pAttachments = color_blend_states.data()
	};
	pipeline_info.pColorBlendState = &color_blend_state;
	
	VkViewport viewport {
		.width = static_cast<float>(pass_width == 0 ? context.swapchain.extent.width : pass_width),
		.height = static_cast<float>(pass_height == 0 ? context.swapchain.extent.height : pass_height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	VkRect2D scissor {
		.extent = (pass_width == 0 || pass_height == 0) ?
			context.swapchain.extent :
			VkExtent2D {
				.width = pass_width,
				.height = pass_height
			}
	};

	// Flip front face for offscreen passes
	if(!contains_render_output) {
		rasterization_state.frontFace = VK_FRONT_FACE_CLOCKWISE;
	}
	// Flip viewport for final presentation
	if(contains_render_output) {
		viewport.y = viewport.height;
		viewport.height = -viewport.height;
	}

	VkPipelineViewportStateCreateInfo viewport_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &viewport,
		.scissorCount = 1,
		.pScissors = &scissor
	};
	pipeline_info.pViewportState = &viewport_state_info;


	VK_CHECK(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
		nullptr, &pipeline.handle));

	for(VkPipelineShaderStageCreateInfo &pipeline_shader_stage_info : shader_stage_infos) {
		vkDestroyShaderModule(context.device, pipeline_shader_stage_info.module, nullptr);
	}

	return pipeline;
}

RaytracingPipeline CreateRaytracingPipeline(VulkanContext &context, ResourceManager &resource_manager,
	RenderPass &render_pass, RaytracingPipelineDescription description,
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR &raytracing_properties) {
	RaytracingPipeline pipeline {
		.description = description
	};

	uint32_t shader_slot = 0;
	std::vector<VkPipelineShaderStageCreateInfo> shader_stage_infos {
		VkUtils::PipelineShaderStageCreateInfo(context.device, description.raygen_shader,
			VK_SHADER_STAGE_RAYGEN_BIT_KHR),
	};
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups {
		VkRayTracingShaderGroupCreateInfoKHR {
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = shader_slot++,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		}
	};

	for(const char *miss_shader : description.miss_shaders) {
		shader_stage_infos.emplace_back(
			VkUtils::PipelineShaderStageCreateInfo(context.device, miss_shader, VK_SHADER_STAGE_MISS_BIT_KHR)
		);
		shader_groups.emplace_back(VkRayTracingShaderGroupCreateInfoKHR {
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = shader_slot++,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		});
	}
	uint32_t hit_shader_count = 0;
	for(HitShader hit_shader : description.hit_shaders) {
		uint32_t closest_hit_shader_slot = VK_SHADER_UNUSED_KHR;
		uint32_t any_hit_shader_slot = VK_SHADER_UNUSED_KHR;
		if(hit_shader.closest_hit) {
			shader_stage_infos.emplace_back(
				VkUtils::PipelineShaderStageCreateInfo(context.device, hit_shader.closest_hit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
			);
			closest_hit_shader_slot = shader_slot++;
			++hit_shader_count;
		}
		if(hit_shader.any_hit) {
			shader_stage_infos.emplace_back(
				VkUtils::PipelineShaderStageCreateInfo(context.device, hit_shader.any_hit, VK_SHADER_STAGE_ANY_HIT_BIT_KHR)
			);
			any_hit_shader_slot = shader_slot++;
			++hit_shader_count;
		}
		shader_groups.emplace_back(VkRayTracingShaderGroupCreateInfoKHR {
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.generalShader = VK_SHADER_UNUSED_KHR,
			.closestHitShader = closest_hit_shader_slot,
			.anyHitShader = any_hit_shader_slot,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		});
	}

	std::vector<VkDescriptorSetLayout> descriptor_set_layouts { 
		resource_manager.global_descriptor_set_layout,
		resource_manager.per_frame_descriptor_set_layout
	};
	if(render_pass.descriptor_set_layout != VK_NULL_HANDLE) {
		descriptor_set_layouts.emplace_back(render_pass.descriptor_set_layout);
	}
	VkPipelineLayoutCreateInfo layout_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = static_cast<uint32_t>(descriptor_set_layouts.size()),
		.pSetLayouts = descriptor_set_layouts.data()
	};
	VK_CHECK(vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline.layout));

	VkRayTracingPipelineCreateInfoKHR raytracing_pipeline_info {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		.stageCount = static_cast<uint32_t>(shader_stage_infos.size()),
		.pStages = shader_stage_infos.data(),
		.groupCount = static_cast<uint32_t>(shader_groups.size()),
		.pGroups = shader_groups.data(),
		.maxPipelineRayRecursionDepth = 1,
		.layout = pipeline.layout
	};

	VK_CHECK(vkCreateRayTracingPipelinesKHR(context.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1,
		&raytracing_pipeline_info, nullptr, &pipeline.handle));

	uint32_t group_count = static_cast<uint32_t>(shader_groups.size());
	uint32_t group_handle_size = raytracing_properties.shaderGroupHandleSize;
	uint32_t group_handle_size_aligned = VkUtils::AlignUp(group_handle_size, 
		raytracing_properties.shaderGroupBaseAlignment);
	uint32_t shader_binding_table_size = group_handle_size_aligned * group_count;
	std::vector<uint8_t> shader_group_handles(shader_binding_table_size);
	VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(context.device, pipeline.handle, 0,
		group_count, shader_binding_table_size, shader_group_handles.data()));

	auto create_shader_binding_table = [&](uint64_t offset, uint64_t num_handles) {
		if(num_handles == 0) {
			return ShaderBindingTable {};
		}

		VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(group_handle_size_aligned * num_handles,
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR);
		ShaderBindingTable shader_binding_table {
			.buffer = VkUtils::CreateMappedBuffer(context.allocator, buffer_info)
		};

		uint8_t *mapped_data = reinterpret_cast<uint8_t *>(shader_binding_table.buffer.mapped_data);
		for(int i = 0; i < num_handles; ++i) {
			memcpy(
				mapped_data,
				(shader_group_handles.data() + offset * group_handle_size) + group_handle_size * i,
				group_handle_size
			);
			mapped_data += group_handle_size_aligned;
		}
		shader_binding_table.strided_device_address_region = VkStridedDeviceAddressRegionKHR {
			.deviceAddress = VkUtils::GetDeviceAddress(context.device, shader_binding_table.buffer.handle).deviceAddress,
			.stride = group_handle_size_aligned,
			.size = group_handle_size_aligned * num_handles
		};
		return shader_binding_table;
	};

	pipeline.raygen_sbt = create_shader_binding_table(0, 1);
	pipeline.miss_sbt = create_shader_binding_table(1, description.miss_shaders.size());
	pipeline.hit_sbt = create_shader_binding_table(1 + description.miss_shaders.size(), hit_shader_count);

	for(VkPipelineShaderStageCreateInfo &pipeline_shader_stage_info : shader_stage_infos) {
		vkDestroyShaderModule(context.device, pipeline_shader_stage_info.module, nullptr);
	}

	return pipeline;
}
}
