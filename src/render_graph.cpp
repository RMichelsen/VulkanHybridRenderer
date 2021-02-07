#include "pch.h"
#include "render_graph.h"

#include "pipeline.h"
#include "resource_manager.h"
#include "vulkan_context.h"
#include "vulkan_utils.h"

RenderGraph::RenderGraph(VulkanContext &context) : context(context) {}

void RenderGraph::AddGraphicsPass(const char *render_pass_name, std::vector<TransientResource> dependencies, 
	std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines, 
	GraphicsPassCallback callback) {

	for(TransientResource &resource : dependencies) {
		layout.readers[resource.name].emplace_back(render_pass_name);
	}
	for(TransientResource &resource : outputs) {
		layout.writers[resource.name].emplace_back(render_pass_name);
	}

	assert(!layout.render_pass_descriptions.contains(render_pass_name));
	layout.render_pass_descriptions[render_pass_name] = RenderPassDescription {
		.dependencies = dependencies,
		.outputs = outputs,
		.description = GraphicsPassDescription {
			.pipeline_descriptions = pipelines,
			.callback = callback
		}
	};
}

void RenderGraph::AddRaytracingPass(const char *render_pass_name, std::vector<TransientResource> dependencies, 
	std::vector<TransientResource> outputs, RaytracingPipelineDescription pipeline, 
	RaytracingPassCallback callback) {

	for(TransientResource &resource : dependencies) {
		layout.readers[resource.name].emplace_back(render_pass_name);
	}
	for(TransientResource &resource : outputs) {
		layout.writers[resource.name].emplace_back(render_pass_name);
	}

	assert(!layout.render_pass_descriptions.contains(render_pass_name));
	layout.render_pass_descriptions[render_pass_name] = RenderPassDescription {
		.dependencies = dependencies,
		.outputs = outputs,
		.description = RaytracingPassDescription {
			.pipeline_description = pipeline,
			.callback = callback
		}
	};
}

void RenderGraph::Compile(ResourceManager &resource_manager) {
	FindExecutionOrder();

	std::unordered_map<std::string, std::vector<TransientResource>> resources_to_actualize;
	for(std::string &pass_name : execution_order) {
		RenderPassDescription &pass = layout.render_pass_descriptions[pass_name];
		for(TransientResource &dependency : pass.dependencies) {
			assert(dependency.type != TransientResourceType::Buffer);
			resources_to_actualize[dependency.name].emplace_back(dependency);
		}
		for(TransientResource &output : pass.outputs) {
			assert(output.type != TransientResourceType::Buffer);
			resources_to_actualize[output.name].emplace_back(output);
		}
	}

	for(auto &[name, resources] : resources_to_actualize) {
		assert(!resources.empty());
		if(name == "BACKBUFFER") continue;
		if(images.contains(name)) assert(false);

		switch(resources[0].type) {
		case TransientResourceType::Image: {
			uint32_t width = resources.front().image.width;
			uint32_t height = resources.front().image.height;
			VkFormat format = resources.front().image.format;
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImageUsageFlags usage = 0;

			switch(resources.front().image.type) {
			case TransientImageType::AttachmentImage: {
				layout = VK_IMAGE_LAYOUT_UNDEFINED;
			} break;
			case TransientImageType::SampledImage: {
				layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			} break;
			case TransientImageType::StorageImage: {
				layout = VK_IMAGE_LAYOUT_GENERAL;
			} break;
			}

			for(TransientResource &resource : resources) {
				assert(resource.image.width == width);
				assert(resource.image.height == height);
				assert(resource.image.format == format);
				
				switch(resource.image.type) {
				case TransientImageType::AttachmentImage: {
					usage |= VkUtils::IsDepthFormat(resource.image.format) ?
						VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
						VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
				} break;
				case TransientImageType::SampledImage: {
					usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
				} break;
				case TransientImageType::StorageImage: {
					usage |= VK_IMAGE_USAGE_STORAGE_BIT;
				} break;
				}
			}

			images[name] = resource_manager.Create2DImage(width, height, format, usage, layout);
		} break;
		case TransientResourceType::Buffer: {
			// TODO: Buffers
			assert(false);
		} break;
		}
	}

	std::unordered_map<std::string, ImageAccess> previous_access;

	for(std::string &pass_name : execution_order) {
		RenderPassDescription &pass_description = layout.render_pass_descriptions[pass_name];
		if(std::holds_alternative<GraphicsPassDescription>(pass_description.description)) {
			RenderPass render_pass = CompileGraphicsPass(resource_manager, previous_access,
				pass_description);
			assert(!render_passes.contains(pass_name));
			render_passes[pass_name] = render_pass;
		}
		else if(std::holds_alternative<RaytracingPassDescription>(pass_description.description)) {
			RenderPass render_pass = CompileRaytracingPass(resource_manager, previous_access,
				pass_description);
			assert(!render_passes.contains(pass_name));
			render_passes[pass_name] = render_pass;
		}
	}

	for(auto &[name, access] : previous_access) {
		if(initial_image_access[name].layout != access.layout) {
			finalize_transitions.emplace_back(ImageLayoutTransition {
				.image_name = name,
				.format = access.format,
				.src_layout = access.layout,
				.dst_layout = initial_image_access[name].layout,
				.src_access = access.access_flags,
				.dst_access = initial_image_access[name].access_flags,
				.src_stage = access.stage_flags,
				.dst_stage = initial_image_access[name].stage_flags
			});
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

	for(ImageLayoutTransition &transition : finalize_transitions) {
		VkImageAspectFlags aspect_flags = VkUtils::IsDepthFormat(transition.format) ?
			VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

		VkUtils::InsertImageBarrier(command_buffer, images[transition.image_name].handle,
			aspect_flags, transition.src_layout, transition.dst_layout, transition.src_stage, 
			transition.dst_stage, transition.src_access, transition.dst_access);
	}
}

void RenderGraph::FindExecutionOrder() {
	assert(layout.writers["BACKBUFFER"].size() == 1);

	execution_order = { layout.writers["BACKBUFFER"][0] };
	std::deque<std::string> stack { layout.writers["BACKBUFFER"][0] };
	while(!stack.empty()) {
		RenderPassDescription &pass = layout.render_pass_descriptions[stack.front()];
		stack.pop_front();

		for(TransientResource &dependency : pass.dependencies) {
			for(std::string &writer : layout.writers[dependency.name]) {
				if(std::find(execution_order.begin(), execution_order.end(), writer) == execution_order.end()) {
					execution_order.push_back(writer);
					stack.push_back(writer);
				}
			}
		}
	}

	std::reverse(execution_order.begin(), execution_order.end());
}

RenderPass RenderGraph::CompileGraphicsPass(ResourceManager &resource_manager, 
	std::unordered_map<std::string, ImageAccess> &previous_access, RenderPassDescription &pass_description) {
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
	std::vector<VkAttachmentDescription> attachments;
	std::vector<VkAttachmentReference> color_attachment_refs;
	VkAttachmentReference depth_attachment_ref;
	VkSubpassDescription subpass_description {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
	};
	for(TransientResource &dependency : pass_description.dependencies) {
		switch(dependency.type) {
		case TransientResourceType::Image: {
			assert(dependency.image.type != TransientImageType::AttachmentImage &&
				"Attachment images must be outputs");
			AddSampledOrStorageImage(dependency, render_pass, previous_access, bindings,
				descriptors, VK_ACCESS_SHADER_READ_BIT);
		} break;
		case TransientResourceType::Buffer: {
			// TODO: Add Buffers
			assert(false);
		} break;
		}
	}
	for(TransientResource &output : pass_description.outputs) {
		switch(output.type) {
		case TransientResourceType::Image: {
			switch(output.image.type) {
			case TransientImageType::AttachmentImage: {
				AddAttachmentImage(output, graphics_pass, previous_access, attachments,
					color_attachment_refs, depth_attachment_ref, subpass_description);
			} break;
			case TransientImageType::SampledImage:
			case TransientImageType::StorageImage: {
				AddSampledOrStorageImage(output, render_pass, previous_access, bindings,
					descriptors, VK_ACCESS_SHADER_WRITE_BIT);
			} break;
			}
		} break;
		case TransientResourceType::Buffer: {
			// TODO: Add Buffers
			assert(false);
		} break;
		}
	}

	if(!bindings.empty()) {
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
				.dstBinding = bindings[i].binding,
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
	std::unordered_map<std::string, ImageAccess> &previous_access, RenderPassDescription &pass_description) {
	RaytracingPassDescription &raytracing_pass_description = 
		std::get<RaytracingPassDescription>(pass_description.description);
	RenderPass render_pass {
		.pass = RaytracingPass {
			.callback = raytracing_pass_description.callback
		}
	};

	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::vector<VkDescriptorImageInfo> descriptors;
	for(TransientResource &dependency : pass_description.dependencies) {
		switch(dependency.type) {
		case TransientResourceType::Image: {
			assert(dependency.image.type != TransientImageType::AttachmentImage &&
				"Attachment images are not allowed in raytracing passes");
			AddSampledOrStorageImage(dependency, render_pass, previous_access, bindings,
				descriptors, VK_ACCESS_SHADER_READ_BIT);
		} break;
		case TransientResourceType::Buffer: {
			// TODO: Add Buffers
			assert(false);
		} break;
		}
	}
	for(TransientResource &output : pass_description.outputs) {
		switch(output.type) {
		case TransientResourceType::Image: {
			assert(output.image.type != TransientImageType::AttachmentImage &&
				"Attachment images are not allowed in raytracing passes");
			AddSampledOrStorageImage(output, render_pass, previous_access, bindings,
				descriptors, VK_ACCESS_SHADER_WRITE_BIT);
		} break;
		case TransientResourceType::Buffer: {
			// TODO: Add Buffers
			assert(false);
		} break;
		}
	}

	if(!pass_description.dependencies.empty() || !pass_description.outputs.empty()) {
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

		if(VkUtils::IsDepthFormat(attachment.image.format)) {
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

	for(ImageLayoutTransition &transition : render_pass.preparation_transitions) {
		VkImageAspectFlags aspect_flags = VkUtils::IsDepthFormat(transition.format) ?
			VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

		VkUtils::InsertImageBarrier(command_buffer, images[transition.image_name].handle,
			aspect_flags, transition.src_layout, transition.dst_layout, transition.src_stage, 
			transition.dst_stage, transition.src_access, transition.dst_access);
	}

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

void RenderGraph::AddSampledOrStorageImage(TransientResource &resource, RenderPass &render_pass,
	std::unordered_map<std::string, ImageAccess> &previous_access, std::vector<VkDescriptorSetLayoutBinding> &bindings,
	std::vector<VkDescriptorImageInfo> &descriptors, VkAccessFlags access_flags) {
	assert(resource.image.type != TransientImageType::AttachmentImage &&
		"Cannot add attachment image as sampled or storage image");

	// TODO: ENSURE CORRECTNESS
	VkPipelineStageFlags pipeline_stage;
	VkShaderStageFlags shader_stage;
	if(std::holds_alternative<GraphicsPass>(render_pass.pass)) {
		pipeline_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		shader_stage = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
	}
	else if(std::holds_alternative<RaytracingPass>(render_pass.pass)) {
		pipeline_stage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
		shader_stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	}

	VkImageLayout layout = VkUtils::GetImageLayoutFromResourceType(resource.image.type,
		resource.image.format);

	ImageAccess access = {
		.layout = layout,
		.format = resource.image.format,
		.access_flags = access_flags,
		.stage_flags = pipeline_stage
	};
	if(previous_access.contains(resource.name)) {
		if(layout != previous_access[resource.name].layout) {
			render_pass.preparation_transitions.emplace_back(ImageLayoutTransition {
				.image_name = resource.name,
				.format = resource.image.format,
				.src_layout = previous_access[resource.name].layout,
				.dst_layout = access.layout,
				.src_access = previous_access[resource.name].access_flags,
				.dst_access = access.access_flags,
				.src_stage = previous_access[resource.name].stage_flags,
				.dst_stage = access.stage_flags
				});
		}
	}
	else {
		initial_image_access[resource.name] = access;
	}
	previous_access[resource.name] = access;

	descriptors.emplace_back(VkDescriptorImageInfo {
		.sampler = resource.image.type == TransientImageType::SampledImage ?
			resource.image.sampled_image.sampler : // TODO: SAMPLER
			VK_NULL_HANDLE,
		.imageView = images[resource.name].view,
		.imageLayout = layout
	});
	bindings.emplace_back(VkDescriptorSetLayoutBinding {
		.binding = resource.image.type == TransientImageType::SampledImage ?
			resource.image.sampled_image.binding :
			resource.image.storage_image.binding,
		.descriptorType = resource.image.type == TransientImageType::SampledImage ?
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER :
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = shader_stage
	});
}

void RenderGraph::AddAttachmentImage(TransientResource &resource, GraphicsPass &graphics_pass,
	std::unordered_map<std::string, ImageAccess> &previous_access, std::vector<VkAttachmentDescription> &attachments,
	std::vector<VkAttachmentReference> &color_attachment_refs, VkAttachmentReference &depth_attachment_ref, 
	VkSubpassDescription &subpass_description) {
	graphics_pass.attachments.emplace_back(resource);

	bool is_backbuffer = resource.name == "BACKBUFFER";
	bool is_depth_attachment = VkUtils::IsDepthFormat(resource.image.format);
	VkImageLayout layout = is_depth_attachment ?
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	VkAccessFlags access_flags = is_depth_attachment ?
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT :
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkAttachmentDescription attachment {
		.format = is_backbuffer ?
			context.swapchain.format :
			resource.image.format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = previous_access.contains(resource.name) ? // TODO: PARAM
			VK_ATTACHMENT_LOAD_OP_LOAD :
			VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = previous_access.contains(resource.name) ?
			previous_access[resource.name].layout :
			VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = is_backbuffer ?
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR :
			layout
	};

	if(!is_backbuffer) {
		ImageAccess access {
			.layout = attachment.finalLayout,
			.format = attachment.format,
			.access_flags = access_flags,
			.stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		};
		if(!previous_access.contains(resource.name)) {
			initial_image_access[resource.name] = access;
		}
		previous_access[resource.name] = access;
	}
	if(is_depth_attachment) {
		depth_attachment_ref = VkAttachmentReference {
			.attachment = static_cast<uint32_t>(attachments.size()),
			.layout = layout
		};
		subpass_description.pDepthStencilAttachment = &depth_attachment_ref;
	}
	else {
		color_attachment_refs.emplace_back(VkAttachmentReference {
			.attachment = static_cast<uint32_t>(attachments.size()),
			.layout = layout
		});
	}

	attachments.emplace_back(attachment);
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
