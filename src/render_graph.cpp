#include "pch.h"
#include "render_graph.h"

#include "pipeline.h"
#include "resource_manager.h"
#include "vulkan_context.h"
#include "vulkan_utils.h"

RenderGraph::RenderGraph(VulkanContext &context) : context(context) {}

void RenderGraph::AddGraphicsPass(const char *render_pass_name, std::vector<TransientResource> inputs, 
	std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines, 
	GraphicsPassCallback callback) {

	std::vector<std::string> input_names;
	for(TransientResource &resource : inputs) {
		input_names.emplace_back(resource.name);
		if(!layout.transient_resources.contains(resource.name)) {
			layout.transient_resources[resource.name] = resource;
		}
		layout.readers[resource.name].emplace_back(render_pass_name);
	}

	std::vector<std::string> output_names;
	for(TransientResource &resource : outputs) {
		output_names.emplace_back(resource.name);
		if(!layout.transient_resources.contains(resource.name)) {
			layout.transient_resources[resource.name] = resource;
		}
		layout.writers[resource.name].emplace_back(render_pass_name);
	}

	assert(!layout.render_pass_descriptions.contains(render_pass_name));
	layout.render_pass_descriptions[render_pass_name] = RenderPassDescription {
		.inputs = input_names,
		.outputs = output_names,
		.description = GraphicsPassDescription {
			.pipeline_descriptions = pipelines,
			.callback = callback
		}
	};
}

void RenderGraph::AddRaytracingPass(const char *render_pass_name, std::vector<TransientResource> inputs, 
	std::vector<TransientResource> outputs, RaytracingPipelineDescription pipeline, 
	RaytracingPassCallback callback) {

	std::vector<std::string> input_names;
	for(TransientResource &resource : inputs) {
		input_names.emplace_back(resource.name);
		if(!layout.transient_resources.contains(resource.name)) {
			layout.transient_resources[resource.name] = resource;
		}
		layout.readers[resource.name].emplace_back(render_pass_name);
	}

	std::vector<std::string> output_names;
	for(TransientResource &resource : outputs) {
		output_names.emplace_back(resource.name);
		if(!layout.transient_resources.contains(resource.name)) {
			layout.transient_resources[resource.name] = resource;
		}
		layout.writers[resource.name].emplace_back(render_pass_name);
	}

	assert(!layout.render_pass_descriptions.contains(render_pass_name));
	layout.render_pass_descriptions[render_pass_name] = RenderPassDescription {
		.inputs = input_names,
		.outputs = output_names,
		.description = RaytracingPassDescription {
			.pipeline_description = pipeline,
			.callback = callback
		}
	};
}

void RenderGraph::Compile(ResourceManager &resource_manager) {
	FindExecutionOrder();

	for(std::string &pass_name : execution_order) {
		RenderPassDescription &pass = layout.render_pass_descriptions[pass_name];
		for(std::string &input : pass.inputs) {
			ActualizeTransientResource(resource_manager, layout.transient_resources[input]);
		}
		for(std::string &output : pass.outputs) {
			ActualizeTransientResource(resource_manager, layout.transient_resources[output]);
		}
	}

	std::unordered_map<std::string, VkImageLayout> previous_layouts;

	for(std::string &pass_name : execution_order) {
		RenderPassDescription &pass_description = layout.render_pass_descriptions[pass_name];
		if(std::holds_alternative<GraphicsPassDescription>(pass_description.description)) {
			RenderPass render_pass = CompileGraphicsPass(resource_manager, previous_layouts,
				pass_description);
			assert(!render_passes.contains(pass_name));
			render_passes[pass_name] = render_pass;
		}
		else if(std::holds_alternative<RaytracingPassDescription>(pass_description.description)) {
			RenderPass render_pass = CompileRaytracingPass(resource_manager, previous_layouts,
				pass_description);
			assert(!render_passes.contains(pass_name));
			render_passes[pass_name] = render_pass;
		}
	}
}

void RenderGraph::Execute(ResourceManager &resource_manager, VkCommandBuffer command_buffer, 
	uint32_t resource_idx, uint32_t image_idx) {
	for(std::string &pass_name : execution_order) {
		assert(render_passes.contains(pass_name));
		RenderPass &render_pass = render_passes[pass_name];

		if(std::holds_alternative<GraphicsPass>(render_pass.pass)) {
			ExecuteGraphicsPass(resource_manager, command_buffer, resource_idx,
				image_idx, render_pass);
		}
		else if(std::holds_alternative<RaytracingPass>(render_pass.pass)) {
			ExecuteRaytracingPass(resource_manager, command_buffer, render_pass);
		}

	}
}

void RenderGraph::FindExecutionOrder() {
	assert(layout.writers["BACKBUFFER"].size() == 1);

	execution_order = { layout.writers["BACKBUFFER"][0] };
	std::deque<std::string> stack { layout.writers["BACKBUFFER"][0] };
	while(!stack.empty()) {
		RenderPassDescription &pass = layout.render_pass_descriptions[stack.front()];
		stack.pop_front();

		for(std::string &input : pass.inputs) {
			for(std::string &writer : layout.writers[input]) {
				if(std::find(execution_order.begin(), execution_order.end(), writer) == execution_order.end()) {
					execution_order.push_back(writer);
					stack.push_back(writer);
				}
			}
		}
	}

	std::reverse(execution_order.begin(), execution_order.end());
}

void RenderGraph::ActualizeTransientResource(ResourceManager &resource_manager, 
	TransientResource &resource) {

	switch(resource.type) {
	case TransientResourceType::Texture: {
		if(!images.contains(resource.name) && resource.name != "BACKBUFFER") {
			images[resource.name] = resource_manager.CreateTransientTexture(
				resource.texture.width, resource.texture.height, resource.texture.format);
		}
	} break;
	case TransientResourceType::StorageImage: {
		assert(resource.name != "BACKBUFFER");
		if(!images.contains(resource.name)) {
			images[resource.name] = resource_manager.CreateTransientStorageImage(
				resource.texture.width, resource.texture.height, resource.texture.format);
		}
	} break;
	case TransientResourceType::Buffer: {
		// TODO: Add Buffers
		assert(false);
	} break;
	}

}

RenderPass RenderGraph::CompileGraphicsPass(ResourceManager &resource_manager, 
	std::unordered_map<std::string, VkImageLayout> &previous_layouts, RenderPassDescription &pass_description) {
	GraphicsPassDescription &graphics_pass_description = 
		std::get<GraphicsPassDescription>(pass_description.description);
	RenderPass render_pass {
		.pass = GraphicsPass {
			.callback = graphics_pass_description.callback
		}
	};
	GraphicsPass &graphics_pass = std::get<GraphicsPass>(render_pass.pass);

	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::vector<VkDescriptorImageInfo> descriptors;
	uint32_t binding = 0;
	for(std::string &input : pass_description.inputs) {
		VkDescriptorType descriptor_type;
		switch(layout.transient_resources[input].type) {
		case TransientResourceType::Texture: {
			if(VkUtils::IsDepthFormat(layout.transient_resources[input].texture.format)) {
				continue;
			}
			descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptors.emplace_back(VkDescriptorImageInfo {
				.sampler = resource_manager.sampler,
				.imageView = images[input].view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			});
		} break;
		case TransientResourceType::StorageImage: {
			descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			descriptors.emplace_back(VkDescriptorImageInfo {
				.imageView = images[input].view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			});
		} break;
		case TransientResourceType::Buffer: {
			// TODO: Add Buffers
			assert(false);
		} break;
		}

		bindings.emplace_back(VkDescriptorSetLayoutBinding {
			.binding = binding++,
			.descriptorType = descriptor_type,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT // TODO: PARAM
		});
	}
	if(!pass_description.inputs.empty()) {
		VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		VK_CHECK(vkCreateDescriptorSetLayout(context.device, &descriptor_set_layout_info,
			nullptr, &render_pass.descriptor_set_layout));
		VkDescriptorSetAllocateInfo descriptor_set_alloc_info {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = resource_manager.transient_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &render_pass.descriptor_set_layout
		};
		VK_CHECK(vkAllocateDescriptorSets(context.device, &descriptor_set_alloc_info,
			&render_pass.descriptor_set));

		std::vector<VkWriteDescriptorSet> write_descriptor_sets;
		for(uint32_t i = 0; i < descriptors.size(); ++i) {
			write_descriptor_sets.emplace_back(VkWriteDescriptorSet {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = render_pass.descriptor_set,
				.dstBinding = i,
				.descriptorCount = 1,
				.descriptorType = descriptors[i].sampler ? 
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : 
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.pImageInfo = &descriptors[i]
			});
		}

		vkUpdateDescriptorSets(context.device, static_cast<uint32_t>(write_descriptor_sets.size()),
			write_descriptor_sets.data(), 0, nullptr);
	}

	VkSubpassDescription subpass_description {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
	};
	std::vector<VkAttachmentDescription> attachments;
	std::vector<VkAttachmentReference> color_attachment_refs;
	for(std::string &output : pass_description.outputs) {
		bool is_backbuffer = output == "BACKBUFFER";

		if(!previous_layouts.contains(output)) {
			previous_layouts[output] = VK_IMAGE_LAYOUT_UNDEFINED;
		}

		TransientResource &resource = layout.transient_resources[output];
		// TODO: Add Buffers
		assert(resource.type == TransientResourceType::Texture);
		graphics_pass.attachments.emplace_back(resource);

		if(VkUtils::IsDepthFormat(resource.texture.format)) {
			VkAttachmentDescription attachment {
				.format = resource.texture.format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = previous_layouts[output] == VK_IMAGE_LAYOUT_UNDEFINED ?
					VK_ATTACHMENT_LOAD_OP_CLEAR :
					VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = previous_layouts[output],
				.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
			};
			VkAttachmentReference attachment_ref {
				.attachment = static_cast<uint32_t>(attachments.size()),
				.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
			};
			attachments.emplace_back(attachment);
			subpass_description.pDepthStencilAttachment = &attachment_ref;
			previous_layouts[output] = attachment.finalLayout;
		}
		else {
			VkAttachmentDescription attachment {
				.format = is_backbuffer ?
					context.swapchain.format :
					layout.transient_resources[output].texture.format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = previous_layouts[output] == VK_IMAGE_LAYOUT_UNDEFINED ?
					VK_ATTACHMENT_LOAD_OP_CLEAR :
					VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = previous_layouts[output],
				.finalLayout = is_backbuffer ?
					VK_IMAGE_LAYOUT_PRESENT_SRC_KHR :
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			};
			VkAttachmentReference attachment_ref {
				.attachment = static_cast<uint32_t>(attachments.size()),
				.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			};
			attachments.emplace_back(attachment);
			color_attachment_refs.emplace_back(attachment_ref);
			previous_layouts[output] = attachment.finalLayout;
		}
	}

	subpass_description.colorAttachmentCount = static_cast<uint32_t>(color_attachment_refs.size());
	subpass_description.pColorAttachments = color_attachment_refs.data();
	VkSubpassDependency subpass_dependency {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};

	VkRenderPassCreateInfo render_pass_info {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = static_cast<uint32_t>(attachments.size()),
		.pAttachments = attachments.data(),
		.subpassCount = 1,
		.pSubpasses = &subpass_description,
		.dependencyCount = 1,
		.pDependencies = &subpass_dependency
	};

	VK_CHECK(vkCreateRenderPass(context.device, &render_pass_info, nullptr, &graphics_pass.handle));

	for(GraphicsPipelineDescription &pipeline_description : graphics_pass_description.pipeline_descriptions) {
		assert(!graphics_pipelines.contains(pipeline_description.name));

		graphics_pipelines[pipeline_description.name] = VkUtils::CreateGraphicsPipeline(context,
			resource_manager, render_pass, pipeline_description);
	}

	return render_pass;
}

RenderPass RenderGraph::CompileRaytracingPass(ResourceManager &resource_manager,
	std::unordered_map<std::string, VkImageLayout> &previous_layouts, RenderPassDescription &pass_description) {
	RaytracingPassDescription &raytracing_pass_description = 
		std::get<RaytracingPassDescription>(pass_description.description);
	RenderPass render_pass {
		.pass = RaytracingPass {
			.callback = raytracing_pass_description.callback
		}
	};

	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::vector<VkDescriptorImageInfo> descriptors;
	uint32_t binding = 0;
	for(std::string &input : pass_description.inputs) {
		if(!previous_layouts.contains(input)) {
			previous_layouts[input] = VK_IMAGE_LAYOUT_UNDEFINED;
		}
		if(previous_layouts[input] != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			std::get<RaytracingPass>(render_pass.pass).preparation_transitions.emplace_back(
				ImageLayoutTransition {
					.resource = layout.transient_resources[input],
					.src_layout = previous_layouts[input],
					.dst_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			);
		}

		VkDescriptorType descriptor_type;
		switch(layout.transient_resources[input].type) {
		case TransientResourceType::Texture: {
			descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptors.emplace_back(VkDescriptorImageInfo {
				.sampler = resource_manager.sampler,
				.imageView = images[input].view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				});
		} break;
		case TransientResourceType::StorageImage: {
			descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			descriptors.emplace_back(VkDescriptorImageInfo {
				.imageView = images[input].view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				});
		} break;
		case TransientResourceType::Buffer: {
			// TODO: Add Buffers
			assert(false);
		} break;
		}

		bindings.emplace_back(VkDescriptorSetLayoutBinding {
			.binding = binding++,
			.descriptorType = descriptor_type,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
				VK_SHADER_STAGE_MISS_BIT_KHR // TODO: PARAM
		});
	}
	for(std::string &output : pass_description.outputs) {
		if(!previous_layouts.contains(output)) {
			previous_layouts[output] = VK_IMAGE_LAYOUT_UNDEFINED;
		}
		if(previous_layouts[output] != VK_IMAGE_LAYOUT_GENERAL) {
			std::get<RaytracingPass>(render_pass.pass).preparation_transitions.emplace_back(
				ImageLayoutTransition {
					.resource = layout.transient_resources[output],
					.src_layout = previous_layouts[output],
					.dst_layout = VK_IMAGE_LAYOUT_GENERAL
				}
			);
		}

		VkDescriptorType descriptor_type;
		switch(layout.transient_resources[output].type) {
		case TransientResourceType::Texture: {
			assert(false);
		} break;
		case TransientResourceType::StorageImage: {
			descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			descriptors.emplace_back(VkDescriptorImageInfo {
				.imageView = images[output].view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			});
		} break;
		case TransientResourceType::Buffer: {
			// TODO: Add Buffers
			assert(false);
		} break;
		}

		bindings.emplace_back(VkDescriptorSetLayoutBinding {
			.binding = binding++,
			.descriptorType = descriptor_type,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
				VK_SHADER_STAGE_MISS_BIT_KHR // TODO: PARAM
		});
	}

	if(!pass_description.inputs.empty() || !pass_description.outputs.empty()) {
		VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data()
		};
		VK_CHECK(vkCreateDescriptorSetLayout(context.device, &descriptor_set_layout_info,
			nullptr, &render_pass.descriptor_set_layout));
		VkDescriptorSetAllocateInfo descriptor_set_alloc_info {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = resource_manager.transient_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &render_pass.descriptor_set_layout
		};
		VK_CHECK(vkAllocateDescriptorSets(context.device, &descriptor_set_alloc_info,
			&render_pass.descriptor_set));

		std::vector<VkWriteDescriptorSet> write_descriptor_sets;
		for(uint32_t i = 0; i < descriptors.size(); ++i) {
			write_descriptor_sets.emplace_back(VkWriteDescriptorSet {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = render_pass.descriptor_set,
				.dstBinding = i,
				.descriptorCount = 1,
				.descriptorType = descriptors[i].sampler ?
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER :
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.pImageInfo = &descriptors[i]
			});
		}

		vkUpdateDescriptorSets(context.device, static_cast<uint32_t>(write_descriptor_sets.size()),
			write_descriptor_sets.data(), 0, nullptr);
	}

	assert(!raytracing_pipelines.contains(raytracing_pass_description.pipeline_description.name));
	raytracing_pipelines[raytracing_pass_description.pipeline_description.name] = VkUtils::CreateRaytracingPipeline(
		context, resource_manager, render_pass, raytracing_pass_description.pipeline_description,
		context.gpu.raytracing_properties);

	return render_pass;
}

void RenderGraph::ExecuteGraphicsPass(ResourceManager &resource_manager, VkCommandBuffer command_buffer,
	uint32_t resource_idx, uint32_t image_idx, RenderPass &render_pass) {
	GraphicsPass &graphics_pass = std::get<GraphicsPass>(render_pass.pass);
	VkFramebuffer &framebuffer = graphics_pass.framebuffers[resource_idx];

	// Delete previous framebuffer
	if(framebuffer != VK_NULL_HANDLE) {
		vkDestroyFramebuffer(context.device, framebuffer, nullptr);
		framebuffer = VK_NULL_HANDLE;
	}

	std::vector<VkImageView> image_views;
	std::vector<VkClearValue> clear_values;
	for(TransientResource &attachment : graphics_pass.attachments) {
		if(attachment.name == "BACKBUFFER") {
			image_views.emplace_back(context.swapchain.image_views[image_idx]);
		}
		else {
			image_views.emplace_back(images[attachment.name].view);
		}

		if(VkUtils::IsDepthFormat(attachment.texture.format)) {
			clear_values.emplace_back(VkClearValue {
				.depthStencil = { 1.0f, 0 }
				});
		}
		else {
			clear_values.emplace_back(VkClearValue {
				.color = { 0.2f, 0.2f, 0.2f, 1.0f }
				});
		}
	}

	VkFramebufferCreateInfo framebuffer_info {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = graphics_pass.handle,
		.attachmentCount = static_cast<uint32_t>(image_views.size()),
		.pAttachments = image_views.data(),
		.width = context.swapchain.extent.width,
		.height = context.swapchain.extent.height,
		.layers = 1
	};

	VK_CHECK(vkCreateFramebuffer(context.device, &framebuffer_info, nullptr, &framebuffer));

	VkRenderPassBeginInfo render_pass_begin_info {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = graphics_pass.handle,
		.framebuffer = framebuffer,
		.renderArea = VkRect2D {
			.offset = VkOffset2D {.x = 0, .y = 0 },
			.extent = context.swapchain.extent
		},
		.clearValueCount = static_cast<uint32_t>(clear_values.size()),
		.pClearValues = clear_values.data()
	};

	vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	graphics_pass.callback(
		[&](std::string pipeline_name, GraphicsPipelineExecutionCallback execute_pipeline) {
			GraphicsPipeline &pipeline = graphics_pipelines[pipeline_name];
			
			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipeline.layout, 0, 1, &resource_manager.global_descriptor_set, 0, nullptr);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipeline.layout, 1, 1, &resource_manager.per_frame_descriptor_set, 0, nullptr);
			if(render_pass.descriptor_set != VK_NULL_HANDLE) {
				vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout,
					2, 1, &render_pass.descriptor_set, 0, nullptr);
			}
			GraphicsPipelineExecutionContext execution_context(command_buffer, resource_manager, render_pass, pipeline);
			execute_pipeline(execution_context);
		}
	);

	vkCmdEndRenderPass(command_buffer);
}

void RenderGraph::ExecuteRaytracingPass(ResourceManager &resource_manager, VkCommandBuffer command_buffer,
	RenderPass &render_pass) {
	RaytracingPass &raytracing_pass = std::get<RaytracingPass>(render_pass.pass);

	raytracing_pass.callback(
		[&](std::string pipeline_name, RaytracingPipelineExecutionCallback execute_pipeline) {
			RaytracingPipeline &pipeline = raytracing_pipelines[pipeline_name];

			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.handle);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
				pipeline.layout, 0, 1, &resource_manager.global_descriptor_set, 0, nullptr);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
				pipeline.layout, 1, 1, &resource_manager.per_frame_descriptor_set, 0, nullptr);
			if(render_pass.descriptor_set != VK_NULL_HANDLE) {
				vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.layout,
					2, 1, &render_pass.descriptor_set, 0, nullptr);
			}

			RaytracingPipelineExecutionContext execution_context(command_buffer, resource_manager, render_pass, pipeline);
			execute_pipeline(execution_context);
		}
	);
}

void GraphicsPipelineExecutionContext::BindGlobalVertexAndIndexBuffers() {
	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &resource_manager.global_vertex_buffer.handle, &offset);
	vkCmdBindIndexBuffer(command_buffer, resource_manager.global_index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
}

void GraphicsPipelineExecutionContext::DrawIndexed(uint32_t index_count, uint32_t instance_count, 
	uint32_t first_index, uint32_t vertex_offset, uint32_t first_instance) {
	vkCmdDrawIndexed(command_buffer, index_count, instance_count, first_index, vertex_offset, first_instance);
}

void GraphicsPipelineExecutionContext::Draw(uint32_t vertex_count, uint32_t instance_count, 
	uint32_t first_vertex, uint32_t first_instance) {
	vkCmdDraw(command_buffer, vertex_count, instance_count, first_vertex, instance_count);
}

void RaytracingPipelineExecutionContext::TraceRays(uint32_t width, uint32_t height) {
	VkStridedDeviceAddressRegionKHR raygen_sbt {
		.deviceAddress = pipeline.shader_binding_table_address + 0 * pipeline.shader_group_size,
		.stride = pipeline.shader_group_size,
		.size = pipeline.shader_group_size
	};
	VkStridedDeviceAddressRegionKHR miss_sbt {
		.deviceAddress = pipeline.shader_binding_table_address + 1 * pipeline.shader_group_size,
		.stride = pipeline.shader_group_size,
		.size = pipeline.shader_group_size
	};
	VkStridedDeviceAddressRegionKHR hit_sbt {
		.deviceAddress = pipeline.shader_binding_table_address + 2 * pipeline.shader_group_size,
		.stride = pipeline.shader_group_size,
		.size = pipeline.shader_group_size
	};
	VkStridedDeviceAddressRegionKHR callable_sbt {};

	// TODO: Move callback out of resource manager?
	resource_manager.vkCmdTraceRaysKHR(command_buffer, &raygen_sbt, &miss_sbt, &hit_sbt, 
		&callable_sbt, width, height, 1);
}
