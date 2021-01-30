#include "pch.h"
#include "renderer.h"
#include "resource_manager.h"
#include "scene_loader.h"
#include "shader_compiler.h"
#include "vulkan_context.h"

Renderer::Renderer(HINSTANCE hinstance, HWND hwnd) {
	context = std::make_unique<VulkanContext>(hinstance, hwnd);
	resource_manager = std::make_unique<ResourceManager>(*context);
	scene = SceneLoader::LoadScene(*resource_manager, "data/models/Sponza/glTF/Sponza.gltf");
	CreatePipeline();
}

Renderer::~Renderer() {
	resource_manager.reset();
	context.reset();
}

void Renderer::Present() {
	static uint32_t resource_idx = 0;
	FrameResources &resources = context->frame_resources[resource_idx];

	VK_CHECK(vkWaitForFences(context->device, 1, &resources.fence, VK_TRUE, UINT64_MAX));
	VK_CHECK(vkResetFences(context->device, 1, &resources.fence));

	uint32_t image_idx;
	VkResult result = vkAcquireNextImageKHR(context->device, context->swapchain.handle, 
		UINT64_MAX, resources.image_available, VK_NULL_HANDLE, &image_idx);
	if(result == VK_ERROR_OUT_OF_DATE_KHR) {
		// TODO: Recreate swapchain
		return;
	}
	VK_CHECK(result);

	Render(resources, image_idx);

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submit_info {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount	= 1,
		.pWaitSemaphores = &resources.image_available,
		.pWaitDstStageMask = &wait_stage,
		.commandBufferCount = 1,
		.pCommandBuffers = &resources.commandbuffer,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &resources.render_finished	
	};

	VK_CHECK(vkQueueSubmit(context->graphics_queue, 1, &submit_info, resources.fence));

	VkPresentInfoKHR present_info {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &resources.render_finished,
		.swapchainCount = 1,
		.pSwapchains = &context->swapchain.handle,
		.pImageIndices = &image_idx,
	};

	result = vkQueuePresentKHR(context->graphics_queue, &present_info);
	if(result == VK_ERROR_OUT_OF_DATE_KHR) {
		// TODO: Recreate swapchain
		return;
	}
	VK_CHECK(result);

	resource_idx = (resource_idx + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::Render(FrameResources &resources, uint32_t image_idx) {
	VkCommandBufferBeginInfo commandbuffer_begin_info {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	VK_CHECK(vkBeginCommandBuffer(resources.commandbuffer, &commandbuffer_begin_info));

	// Delete previous framebuffer
	if(resources.framebuffer != VK_NULL_HANDLE) {
		vkDestroyFramebuffer(context->device, resources.framebuffer, nullptr);
		resources.framebuffer = VK_NULL_HANDLE;
	}

	std::array<VkImageView, 2> attachments {
		context->swapchain.image_views[image_idx],
		context->depth_texture.image_view
	};
	VkFramebufferCreateInfo framebuffer_info {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = context->render_pass,
		.attachmentCount = static_cast<uint32_t>(attachments.size()),
		.pAttachments = attachments.data(),
		.width = context->swapchain.extent.width,	
		.height = context->swapchain.extent.height,	
		.layers = 1
	};
	VK_CHECK(vkCreateFramebuffer(context->device, &framebuffer_info, nullptr, 
		&resources.framebuffer));

	std::array<VkClearValue, 2> clear_values {
		VkClearValue {
			.color = { 0.2f, 0.2f, 0.2f, 1.0f }
		},
		VkClearValue {
			.depthStencil = { 1.0f, 0 }
		}
	};
	VkRenderPassBeginInfo render_pass_begin_info {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context->render_pass,
		.framebuffer = resources.framebuffer,
		.renderArea = VkRect2D {
			.offset = VkOffset2D { .x = 0, .y = 0 },
			.extent = context->swapchain.extent
		},
		.clearValueCount = static_cast<uint32_t>(clear_values.size()),
		.pClearValues = clear_values.data()
	};

	vkCmdBeginRenderPass(resources.commandbuffer, &render_pass_begin_info, 
		VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(resources.commandbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline);

	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(resources.commandbuffer, 0, 1, 
		&resource_manager->global_vertex_buffer.handle, &offset);
	vkCmdBindIndexBuffer(resources.commandbuffer, 
		resource_manager->global_index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
	vkCmdBindDescriptorSets(resources.commandbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline_layout, 0, 1, &resource_manager->descriptor_set, 0, nullptr);

	glm::mat4 P = glm::perspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
	glm::mat4 V = glm::lookAt(glm::vec3(4.0f, 2.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f));

	for(Mesh &mesh : scene.meshes) {
		for(Primitive &primitive : mesh.primitives) {
			PushConstants push_constants {
				.MVP = P * V * primitive.transform,
				.texture = primitive.texture
			};
			vkCmdPushConstants(resources.commandbuffer, pipeline_layout, 
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 
				sizeof(PushConstants), &push_constants);

			vkCmdDrawIndexed(resources.commandbuffer, primitive.index_count,
				1, primitive.index_offset, primitive.vertex_offset, 0);
		}
	}

	vkCmdEndRenderPass(resources.commandbuffer);

	VK_CHECK(vkEndCommandBuffer(resources.commandbuffer));
}

void Renderer::CreatePipeline() {
	std::vector<uint32_t> vertex_bytecode = 
		ShaderCompiler::CompileShader("data/shaders/default.vert", VK_SHADER_STAGE_VERTEX_BIT);
	std::vector<uint32_t> fragment_bytecode = 
		ShaderCompiler::CompileShader("data/shaders/default.frag", VK_SHADER_STAGE_FRAGMENT_BIT);

	VkShaderModule vertex_shader;
	VkShaderModuleCreateInfo vertex_shader_module_info {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = vertex_bytecode.size() * sizeof(uint32_t),
		.pCode = vertex_bytecode.data()
	};
	VK_CHECK(vkCreateShaderModule(context->device, &vertex_shader_module_info, 
		nullptr, &vertex_shader));
	VkShaderModule fragment_shader;
	VkShaderModuleCreateInfo fragment_shader_module_info {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = fragment_bytecode.size() * sizeof(uint32_t),
		.pCode = fragment_bytecode.data()
	};
	VK_CHECK(vkCreateShaderModule(context->device, &fragment_shader_module_info, \
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
		.y = static_cast<float>(context->swapchain.extent.height),
		.width = static_cast<float>(context->swapchain.extent.width),
		.height = -static_cast<float>(context->swapchain.extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	VkRect2D scissor {
		.extent = context->swapchain.extent
	};
	VkPipelineViewportStateCreateInfo viewport_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &viewport,
		.scissorCount = 1,
		.pScissors = &scissor
	};
	VkPipelineRasterizationStateCreateInfo rasterization_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_FRONT_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.lineWidth = 1.0f
	};
	VkPipelineMultisampleStateCreateInfo multisample_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};
	VkPipelineDepthStencilStateCreateInfo depth_stencil_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};
	VkPipelineColorBlendAttachmentState color_blend_attachment_state {
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
	};
	VkPipelineColorBlendStateCreateInfo color_blend_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attachment_state,
	};
	VkPushConstantRange push_constant_range {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(PushConstants)
	};
	VkPipelineLayoutCreateInfo layout_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &resource_manager->descriptor_set_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_constant_range
	};
	VK_CHECK(vkCreatePipelineLayout(context->device, &layout_info, nullptr, &pipeline_layout));

	VkGraphicsPipelineCreateInfo pipeline_info {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = static_cast<uint32_t>(shader_stage_infos.size()),
		.pStages = shader_stage_infos.data(),
		.pVertexInputState = &vertex_input_state_info,
		.pInputAssemblyState = &input_assembly_state_info,
		.pViewportState = &viewport_state_info,
		.pRasterizationState = &rasterization_state_info,
		.pMultisampleState = &multisample_state_info,
		.pDepthStencilState = &depth_stencil_state_info,
		.pColorBlendState = &color_blend_state_info,
		.layout = pipeline_layout,
		.renderPass = context->render_pass,
		.subpass = 0
	};
	VK_CHECK(vkCreateGraphicsPipelines(context->device, VK_NULL_HANDLE, 1, &pipeline_info,
		nullptr, &pipeline));
}
