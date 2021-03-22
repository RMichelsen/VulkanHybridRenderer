#include "pch.h"
#include "render_graph.h"

#include "rendering_backend/pipeline.h"
#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"
#include "render_graph/compute_execution_context.h"
#include "render_graph/graphics_execution_context.h"
#include "render_graph/raytracing_execution_context.h"

RenderGraph::RenderGraph(VulkanContext &context, ResourceManager &resource_manager) : 
	context(context), 
	resource_manager(resource_manager) {}

void RenderGraph::DestroyResources() {
	VK_CHECK(vkDeviceWaitIdle(context.device));

	for(auto &[_, render_pass] : passes) {
		vkDestroyDescriptorSetLayout(context.device, render_pass.descriptor_set_layout, nullptr);
		if(std::holds_alternative<GraphicsPass>(render_pass.pass)) {
			GraphicsPass &graphics_pass = std::get<GraphicsPass>(render_pass.pass);
			for(VkFramebuffer &framebuffer : graphics_pass.framebuffers) {
				vkDestroyFramebuffer(context.device, framebuffer, nullptr);
			}
			vkDestroyRenderPass(context.device, graphics_pass.handle, nullptr);
		}
	}

	for(auto &[_, pipeline] : graphics_pipelines) {
		vkDestroyPipelineLayout(context.device, pipeline.layout, nullptr);
		vkDestroyPipeline(context.device, pipeline.handle, nullptr);
	}

	for(auto &[_, pipeline] : compute_pipelines) {
		vkDestroyPipelineLayout(context.device, pipeline.layout, nullptr);
		vkDestroyPipeline(context.device, pipeline.handle, nullptr);
	}

	for(auto &[_, pipeline] : raytracing_pipelines) {
		VkUtils::DestroyMappedBuffer(context.allocator, pipeline.raygen_sbt.buffer);
		if(pipeline.miss_sbt.buffer.handle != VK_NULL_HANDLE) {
			VkUtils::DestroyMappedBuffer(context.allocator, pipeline.miss_sbt.buffer);
		}
		if(pipeline.hit_sbt.buffer.handle != VK_NULL_HANDLE) {
			VkUtils::DestroyMappedBuffer(context.allocator, pipeline.hit_sbt.buffer);
		}
		vkDestroyPipelineLayout(context.device, pipeline.layout, nullptr);
		vkDestroyPipeline(context.device, pipeline.handle, nullptr);
	}

	for(auto &[_, image] : images) {
		VkUtils::DestroyImage(context.device, context.allocator, image);
	}

	readers.clear();
	writers.clear();
	passes.clear();
	pass_descriptions.clear();
	graphics_pipelines.clear();
	raytracing_pipelines.clear();
	compute_pipelines.clear();
	images.clear();
	image_access.clear();
}

void RenderGraph::AddGraphicsPass(const char *render_pass_name, std::vector<TransientResource> dependencies, 
	std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines, 
	GraphicsPassCallback callback) {
	bool set = false;
	bool multisampled_pass = false;
	for(TransientResource &output : outputs) {
		if(output.type == TransientResourceType::Image) {
			if(set) {
				assert(multisampled_pass == output.image.multisampled);
			}
			else {
				multisampled_pass = output.image.multisampled;
				set = true;
			}
		}
	}
	for(GraphicsPipelineDescription &pipeline_description : pipelines) {
		if(set) {
			assert(multisampled_pass == (pipeline_description.multisample_state == MultisampleState::On ? true : false));
		}
		else {
			multisampled_pass = (pipeline_description.multisample_state == MultisampleState::On ? true : false);
			set = true;
		}
	}
	
	RenderPassDescription pass_description {
		.name = render_pass_name,
		.dependencies = dependencies,
		.outputs = outputs,
		.description = GraphicsPassDescription {
			.pipeline_descriptions = pipelines,
			.callback = callback
		}
	};

	assert(!pass_descriptions.contains(render_pass_name));
	pass_descriptions[render_pass_name] = pass_description;
}

void RenderGraph::AddRaytracingPass(const char *render_pass_name, std::vector<TransientResource> dependencies, 
	std::vector<TransientResource> outputs, RaytracingPipelineDescription pipeline, 
	RaytracingPassCallback callback) {
	RenderPassDescription pass_description {
		.name = render_pass_name,
		.dependencies = dependencies,
		.outputs = outputs,
		.description = RaytracingPassDescription {
			.pipeline_description = pipeline,
			.callback = callback
		}
	};
	assert(!pass_descriptions.contains(render_pass_name));
	pass_descriptions[render_pass_name] = pass_description;
}

void RenderGraph::AddComputePass(const char *render_pass_name, std::vector<TransientResource> dependencies, 
	std::vector<TransientResource> outputs, ComputePipelineDescription pipeline, ComputePassCallback callback) {
	RenderPassDescription pass_description {
		.name = render_pass_name,
		.dependencies = dependencies,
		.outputs = outputs,
		.description = ComputePassDescription {
			.pipeline_description = pipeline,
			.callback = callback
		}
	};
	assert(!pass_descriptions.contains(render_pass_name));
	pass_descriptions[render_pass_name] = pass_description;
}

void RenderGraph::Build() {
	for(auto &[_, pass_description] : pass_descriptions) {
		for(TransientResource &resource : pass_description.dependencies) {
			readers[resource.name].emplace_back(pass_description.name);
			ActualizeResource(resource, pass_description.name);
		}
		for(TransientResource &resource : pass_description.outputs) {
			writers[resource.name].emplace_back(pass_description.name);
			ActualizeResource(resource, pass_description.name);
		}
		if(std::holds_alternative<GraphicsPassDescription>(pass_description.description)) {
			CreateGraphicsPass(pass_description);
		}
		else if(std::holds_alternative<RaytracingPassDescription>(pass_description.description)) {
			CreateRaytracingPass(pass_description);
		}
		else if(std::holds_alternative<ComputePassDescription>(pass_description.description)) {
			CreateComputePass(pass_description);
		}
	}
}

void RenderGraph::Execute(VkCommandBuffer command_buffer, uint32_t resource_idx, uint32_t image_idx) {
	FindExecutionOrder();
	assert(SanityCheck());

	for(std::string &pass_name : execution_order) {
		assert(passes.contains(pass_name));
		RenderPass &render_pass = passes[pass_name];

		VkDebugUtilsLabelEXT pass_label {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
			.pLabelName = render_pass.name
		};
		vkCmdBeginDebugUtilsLabelEXT(command_buffer, &pass_label);

		InsertBarriers(command_buffer, render_pass);

		if(std::holds_alternative<GraphicsPass>(render_pass.pass)) {
			ExecuteGraphicsPass(command_buffer, resource_idx, image_idx, render_pass);
		}
		else if(std::holds_alternative<RaytracingPass>(render_pass.pass)) {
			ExecuteRaytracingPass(command_buffer, render_pass);
		}
		else if(std::holds_alternative<ComputePass>(render_pass.pass)) {
			ExecuteComputePass(command_buffer, render_pass);
		}

		vkCmdEndDebugUtilsLabelEXT(command_buffer);
	}
}

void RenderGraph::CopyImage(VkCommandBuffer command_buffer, std::string src_image_name, Image dst_image) {
	assert(images.contains(src_image_name));
	assert(image_access.contains(src_image_name));
	Image &src_image = images[src_image_name];
	ImageAccess current_access = image_access[src_image_name];

	if(image_access[src_image_name].layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		VkUtils::InsertImageBarrier(
			command_buffer, src_image.handle, VK_IMAGE_ASPECT_COLOR_BIT,
			current_access.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, current_access.stage_flags,
			VK_PIPELINE_STAGE_TRANSFER_BIT, current_access.access_flags, VK_ACCESS_TRANSFER_READ_BIT
		);
	}
	image_access[src_image_name] = ImageAccess {
		.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.access_flags = VK_ACCESS_TRANSFER_READ_BIT,
		.stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT
	};

	VkUtils::InsertImageBarrier(command_buffer, dst_image.handle, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, VK_ACCESS_TRANSFER_WRITE_BIT);

	VkImageCopy image_copy {
		.srcSubresource = VkImageSubresourceLayers {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.layerCount = 1
		},
		.dstSubresource = VkImageSubresourceLayers {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.layerCount = 1
		},
		.extent = VkExtent3D {
			.width = src_image.width,
			.height = src_image.height,
			.depth = 1
		}
	};
	vkCmdCopyImage(
		command_buffer,
		src_image.handle,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dst_image.handle,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&image_copy
	);

	VkUtils::InsertImageBarrier(command_buffer, dst_image.handle, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

VkFormat RenderGraph::GetImageFormat(std::string image_name) {
	assert(images.contains(image_name));
	return images[image_name].format;
}

std::vector<std::string> RenderGraph::GetColorAttachments() {
	std::vector<std::string> color_attachment_names;
	for(auto &[name, image] : images) {
		if(!VkUtils::IsDepthFormat(image.format) && !name.ends_with("_MSAA")) {
			color_attachment_names.emplace_back(name);
		}
	}
	return color_attachment_names;
}

void RenderGraph::CreateGraphicsPass(RenderPassDescription &pass_description) {
	GraphicsPassDescription &graphics_pass_description =
		std::get<GraphicsPassDescription>(pass_description.description);
	RenderPass render_pass {
		.name = pass_description.name,
		.pass = GraphicsPass {
			.callback = graphics_pass_description.callback
		}
	};
	GraphicsPass &graphics_pass = std::get<GraphicsPass>(render_pass.pass);

	uint32_t color_attachment_count = 0;
	uint32_t total_attachment_count = 0;
	for(TransientResource &output : pass_description.outputs) {
		if(output.type == TransientResourceType::Image &&
			output.image.type == TransientImageType::AttachmentImage) {
			if(!VkUtils::IsDepthFormat(output.image.format)) {
				++color_attachment_count;
			}
			++total_attachment_count;
		}
	}
	std::vector<VkAttachmentDescription> attachments(total_attachment_count);
	std::vector<VkAttachmentReference> color_attachment_refs(color_attachment_count);
	graphics_pass.attachments.resize(total_attachment_count);

	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::vector<VkDescriptorImageInfo> descriptors;
	VkAttachmentReference depth_attachment_ref;
	VkSubpassDescription subpass_description {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
	};
	auto add_resource_to_pass = [&](TransientResource &resource, bool input_resource) {
		if(resource.type == TransientResourceType::Image) {
			switch(resource.image.type) {
			case TransientImageType::AttachmentImage: {
				assert(!input_resource && "Attachment images must be outputs");
				bool is_render_output = !strcmp(resource.name, "RENDER_OUTPUT");
				VkImageLayout layout = VkUtils::GetImageLayoutFromResourceType(resource.image.type,
					resource.image.format);
				graphics_pass.attachments[resource.image.binding] = resource;
				attachments[resource.image.binding] = VkAttachmentDescription {
					.format = is_render_output ? context.swapchain.format : resource.image.format,
					.samples = resource.image.multisampled ? VK_SAMPLE_COUNT_8_BIT : VK_SAMPLE_COUNT_1_BIT,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
					.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
					.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.finalLayout = is_render_output ? (
						resource.image.multisampled ? 
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : 
						VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
					) : layout
				};

				if(VkUtils::IsDepthFormat(resource.image.format)) {
					assert(!subpass_description.pDepthStencilAttachment);
					depth_attachment_ref = VkAttachmentReference {
						.attachment = resource.image.binding,
						.layout = layout
					};
					subpass_description.pDepthStencilAttachment = &depth_attachment_ref;
				}
				else {
					color_attachment_refs[resource.image.binding] = VkAttachmentReference {
						.attachment = resource.image.binding,
						.layout = layout
					};
				}
			} break;
			case TransientImageType::SampledImage: {
				descriptors.emplace_back(
					VkUtils::DescriptorImageInfo(
						images[resource.name].view,
						VkUtils::GetImageLayoutFromResourceType(TransientImageType::SampledImage, resource.image.format),
						resource_manager.default_sampler
					)
				);
				bindings.emplace_back(VkUtils::DescriptorSetLayoutBinding(
					resource.image.binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT));
			} break;
			case TransientImageType::StorageImage: {
				descriptors.emplace_back(VkUtils::DescriptorImageInfo(images[resource.name].view,
					VK_IMAGE_LAYOUT_GENERAL));
				bindings.emplace_back(VkUtils::DescriptorSetLayoutBinding(
					resource.image.binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT));
			} break;
			}
		}
		else if(resource.type == TransientResourceType::Buffer) {
			// TODO: Add Buffers
			assert(false);
		}
	};
	for(TransientResource &dependency : pass_description.dependencies) {
		add_resource_to_pass(dependency, true);
	}

	bool is_multisampled_pass = false;
	for(TransientResource &output : pass_description.outputs) {
		add_resource_to_pass(output, false);
		if(output.type == TransientResourceType::Image) {
			if(output.image.multisampled) {
				is_multisampled_pass = true;
			}
		}
	}

	VkAttachmentReference color_attachment_resolve_ref;
	if(is_multisampled_pass) {
		attachments.emplace_back(VkAttachmentDescription {
			.format = context.swapchain.format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		});
		color_attachment_resolve_ref.attachment = static_cast<uint32_t>(attachments.size()) - 1;
		color_attachment_resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		subpass_description.pResolveAttachments = &color_attachment_resolve_ref;
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

	passes[render_pass.name] = render_pass;
}

void RenderGraph::CreateRaytracingPass(RenderPassDescription &pass_description) {
	RaytracingPassDescription &raytracing_pass_description =
		std::get<RaytracingPassDescription>(pass_description.description);
	RenderPass render_pass {
		.name = pass_description.name,
		.pass = RaytracingPass {
			.callback = raytracing_pass_description.callback
		}
	};

	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::vector<VkDescriptorImageInfo> descriptors;
	auto add_resource_to_pass = [&](TransientResource &resource) {
		if(resource.type == TransientResourceType::Image) {
			assert(resource.image.type != TransientImageType::AttachmentImage &&
				"Attachment images are not allowed in raytracing passes");
			switch(resource.image.type) {
			case TransientImageType::SampledImage: {
				descriptors.emplace_back(
					VkUtils::DescriptorImageInfo(
						images[resource.name].view,
						VkUtils::GetImageLayoutFromResourceType(TransientImageType::SampledImage, resource.image.format),
						resource_manager.default_sampler
					)
				);
				bindings.emplace_back(VkUtils::DescriptorSetLayoutBinding(
					resource.image.binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
					VK_SHADER_STAGE_RAYGEN_BIT_KHR));
			} break;
			case TransientImageType::StorageImage: {
				descriptors.emplace_back(VkUtils::DescriptorImageInfo(images[resource.name].view,
					VK_IMAGE_LAYOUT_GENERAL));
				bindings.emplace_back(VkUtils::DescriptorSetLayoutBinding(
					resource.image.binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 
					VK_SHADER_STAGE_RAYGEN_BIT_KHR));
			} break;
			}
		}
		else if(resource.type == TransientResourceType::Buffer) {
			// TODO: Add Buffers
			assert(false);
		}
	};

	for(TransientResource &dependency : pass_description.dependencies) {
		add_resource_to_pass(dependency);
	}
	for(TransientResource &output : pass_description.outputs) {
		add_resource_to_pass(output);
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

	passes[render_pass.name] = render_pass;
}

void RenderGraph::CreateComputePass(RenderPassDescription &pass_description) {
	ComputePassDescription &compute_pass_description =
		std::get<ComputePassDescription>(pass_description.description);
	RenderPass render_pass {
		.name = pass_description.name,
		.pass = ComputePass {
			.callback = compute_pass_description.callback
		}
	};

	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::vector<VkDescriptorImageInfo> descriptors;
	auto add_resource_to_pass = [&](TransientResource &resource) {
		if(resource.type == TransientResourceType::Image) {
			assert(resource.image.type != TransientImageType::AttachmentImage &&
				"Attachment images are not allowed in compute passes");
			switch(resource.image.type) {
			case TransientImageType::SampledImage: {
				descriptors.emplace_back(
					VkUtils::DescriptorImageInfo(
						images[resource.name].view,
						VkUtils::GetImageLayoutFromResourceType(TransientImageType::SampledImage, resource.image.format),
						resource_manager.default_sampler
					)
				);
				bindings.emplace_back(VkUtils::DescriptorSetLayoutBinding(
					resource.image.binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					VK_SHADER_STAGE_COMPUTE_BIT));
			} break;
			case TransientImageType::StorageImage: {
				descriptors.emplace_back(VkUtils::DescriptorImageInfo(images[resource.name].view,
					VK_IMAGE_LAYOUT_GENERAL));
				bindings.emplace_back(VkUtils::DescriptorSetLayoutBinding(
					resource.image.binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					VK_SHADER_STAGE_COMPUTE_BIT));
			} break;
			}
		}
		else if(resource.type == TransientResourceType::Buffer) {
			// TODO: Add Buffers
			assert(false);
		}
	};

	for(TransientResource &dependency : pass_description.dependencies) {
		add_resource_to_pass(dependency);
	}
	for(TransientResource &output : pass_description.outputs) {
		add_resource_to_pass(output);
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


	// Create compute pipelines of all associated kernels
	for(ComputeKernel &kernel : compute_pass_description.pipeline_description.kernels) {
		assert(!compute_pipelines.contains(kernel.entry) && "Compute kernel entry names must be unique!");
		compute_pipelines[kernel.entry] = VkUtils::CreateComputePipeline(context,
			resource_manager, render_pass, compute_pass_description.pipeline_description.push_constant_description, 
			kernel);
	}

	passes[render_pass.name] = render_pass;
}

void RenderGraph::FindExecutionOrder() {
	assert(writers["RENDER_OUTPUT"].size() == 1);

	// Traverse from the final pass back and insert all dependent passes 
	execution_order = { writers["RENDER_OUTPUT"][0] };
	std::deque<std::string> stack { writers["RENDER_OUTPUT"][0] };
	while(!stack.empty()) {
		RenderPassDescription &pass = pass_descriptions[stack.front()];
		stack.pop_front();

		for(TransientResource &dependency : pass.dependencies) {
			for(std::string &writer : writers[dependency.name]) {
				execution_order.push_back(writer);
				stack.push_back(writer);
			}
		}
	}

	// Reverse the list
	std::reverse(execution_order.begin(), execution_order.end());

	// Prune duplicates
	std::vector<std::string> found;

	std::vector<std::string>::iterator it = execution_order.begin();
	while(it != execution_order.end()) {
		if(std::find(found.begin(), found.end(), *it) == found.end()) {
			found.emplace_back(*it);
			++it;
		}
		else {
			it = execution_order.erase(it);
		}
	}
}

void RenderGraph::InsertBarriers(VkCommandBuffer command_buffer, RenderPass &render_pass) {
	RenderPassDescription &pass_description = pass_descriptions[render_pass.name];
	bool is_graphics_pass = std::holds_alternative<GraphicsPass>(render_pass.pass);
	VkPipelineStageFlags dst_stage = is_graphics_pass ?
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT :
		VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;

	bool transitions_started = false;

	auto insert_barrier_if_needed = [&](TransientResource &resource, 
		VkPipelineStageFlags dst_stage, VkAccessFlags dst_access) {
		ImageAccess current_access = image_access[resource.name];
		VkImageLayout dst_layout = VkUtils::GetImageLayoutFromResourceType(resource.image.type,
			resource.image.format);

		if(strcmp(resource.name, "RENDER_OUTPUT") && current_access.layout != dst_layout) {
			VkImageAspectFlags aspect_flags = VkUtils::IsDepthFormat(resource.image.format) ?
				VK_IMAGE_ASPECT_DEPTH_BIT :
				VK_IMAGE_ASPECT_COLOR_BIT;

			if(!transitions_started) {
				VkDebugUtilsLabelEXT pass_label {
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
					.pLabelName = "Image Transitions"
				};
				vkCmdBeginDebugUtilsLabelEXT(command_buffer, &pass_label);
				transitions_started = true;
			}

			VkUtils::InsertImageBarrier(command_buffer, images[resource.name].handle,
				aspect_flags, current_access.layout, dst_layout, current_access.stage_flags,
				dst_stage, current_access.access_flags, dst_access);

			image_access[resource.name] = ImageAccess {
				.layout = dst_layout,
				.access_flags = dst_access,
				.stage_flags = dst_stage
			};
		}
	};

	for(TransientResource &dependency : pass_description.dependencies) {
		if(dependency.type == TransientResourceType::Image) {
			insert_barrier_if_needed(dependency, dst_stage, VK_ACCESS_SHADER_READ_BIT);
		}
		else if(dependency.type == TransientResourceType::Buffer) {
			// TODO: Buffer
			assert(false);
		}
	}
	for(TransientResource &output : pass_description.outputs) {
		if(output.type == TransientResourceType::Image) {
			if(output.image.type == TransientImageType::AttachmentImage) {
				// Implicit barrier through render pass
				image_access[output.name] = ImageAccess {
					.layout = VkUtils::GetImageLayoutFromResourceType(output.image.type,
						output.image.format),
					.access_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					.stage_flags = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
				};
			}
			else {
				insert_barrier_if_needed(output, dst_stage, VK_ACCESS_SHADER_WRITE_BIT);
			}
		}
		else if(output.type == TransientResourceType::Buffer) {
			// TODO: Buffer
			assert(false);
		}
	}

	if(transitions_started) {
		vkCmdEndDebugUtilsLabelEXT(command_buffer);
	}
}

void RenderGraph::ExecuteGraphicsPass(VkCommandBuffer command_buffer, uint32_t resource_idx, 
	uint32_t image_idx, RenderPass &render_pass) {
	GraphicsPass &graphics_pass = std::get<GraphicsPass>(render_pass.pass);

	VkFramebuffer &framebuffer = graphics_pass.framebuffers[resource_idx];

	// Delete previous framebuffer
	if(framebuffer != VK_NULL_HANDLE) {
		vkDestroyFramebuffer(context.device, framebuffer, nullptr);
		framebuffer = VK_NULL_HANDLE;
	}

	bool is_multisampled_pass = false;
	std::vector<VkImageView> image_views;
	std::vector<VkClearValue> clear_values;
	for(TransientResource &attachment : graphics_pass.attachments) {
		bool is_render_output = !strcmp(attachment.name, "RENDER_OUTPUT");
		if(is_render_output) {
			if(attachment.image.multisampled) {
				image_views.emplace_back(images[std::string(render_pass.name) + "_MSAA"].view);
				is_multisampled_pass = true;
			}
			else {
				image_views.emplace_back(context.swapchain.image_views[image_idx]);
			}
		}
		else {
			image_views.emplace_back(images[attachment.name].view);
		}

		if(VkUtils::IsDepthFormat(attachment.image.format)) {
			clear_values.emplace_back(VkClearValue {
				.depthStencil = VkClearDepthStencilValue { 
					.depth = 1.0f, 
					.stencil = 0 
				}
			});
		}
		else {
			clear_values.emplace_back(VkClearValue {
				.color = VkClearColorValue { 
					.float32 = { 0.2f, 0.2f, 0.2f, 1.0f } 
				}
			});
		}
	}
	if(is_multisampled_pass) {
		image_views.emplace_back(context.swapchain.image_views[image_idx]);
	}

	uint32_t pass_width = graphics_pass.attachments[0].image.width;
	uint32_t pass_height = graphics_pass.attachments[0].image.height;
	VkFramebufferCreateInfo framebuffer_info {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = graphics_pass.handle,
		.attachmentCount = static_cast<uint32_t>(image_views.size()),
		.pAttachments = image_views.data(),
		.width = pass_width == 0 ? context.swapchain.extent.width : pass_width,
		.height = pass_height == 0 ? context.swapchain.extent.height : pass_height,
		.layers = 1
	};

	VK_CHECK(vkCreateFramebuffer(context.device, &framebuffer_info, nullptr, &framebuffer));

	VkRenderPassBeginInfo render_pass_begin_info {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = graphics_pass.handle,
		.framebuffer = framebuffer,
		.renderArea = VkRect2D {
			.offset = VkOffset2D {.x = 0, .y = 0 },
			.extent = (pass_width == 0 || pass_height == 0) ? 
				context.swapchain.extent : 
				VkExtent2D {
					.width = pass_width,
					.height = pass_height
				}
		},
		.clearValueCount = static_cast<uint32_t>(clear_values.size()),
		.pClearValues = clear_values.data()
	};

	vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	graphics_pass.callback(
		[&](std::string pipeline_name, GraphicsExecutionCallback execute_pipeline) {
			GraphicsPipeline &pipeline = graphics_pipelines[pipeline_name];
			
			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipeline.layout, 0, 1, &resource_manager.global_descriptor_set0, 0, nullptr);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipeline.layout, 1, 1, &resource_manager.global_descriptor_set1, 0, nullptr);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipeline.layout, 2, 1, &resource_manager.per_frame_descriptor_set, 0, nullptr);
			if(render_pass.descriptor_set != VK_NULL_HANDLE) {
				vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout,
					3, 1, &render_pass.descriptor_set, 0, nullptr);
			}
			GraphicsExecutionContext execution_context(command_buffer, resource_manager, pipeline);
			execute_pipeline(execution_context);
		}
	);

	vkCmdEndRenderPass(command_buffer);
}

void RenderGraph::ExecuteRaytracingPass(VkCommandBuffer command_buffer, RenderPass &render_pass) {
	RaytracingPass &raytracing_pass = std::get<RaytracingPass>(render_pass.pass);

	raytracing_pass.callback(
		[&](std::string pipeline_name, RaytracingExecutionCallback execute_pipeline) {
			RaytracingPipeline &pipeline = raytracing_pipelines[pipeline_name];

			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.handle);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
				pipeline.layout, 0, 1, &resource_manager.global_descriptor_set0, 0, nullptr);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
				pipeline.layout, 1, 1, &resource_manager.global_descriptor_set1, 0, nullptr);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
				pipeline.layout, 2, 1, &resource_manager.per_frame_descriptor_set, 0, nullptr);
			if(render_pass.descriptor_set != VK_NULL_HANDLE) {
				vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.layout,
					3, 1, &render_pass.descriptor_set, 0, nullptr);
			}

			RaytracingExecutionContext execution_context(command_buffer, resource_manager, pipeline);
			execute_pipeline(execution_context);
		}
	);
}

void RenderGraph::ExecuteComputePass(VkCommandBuffer command_buffer, RenderPass &render_pass) {
	ComputePass &compute_pass = std::get<ComputePass>(render_pass.pass);

	ComputeExecutionContext execution_context(command_buffer, render_pass, *this, resource_manager);
	compute_pass.callback(execution_context);
}

void RenderGraph::ActualizeResource(TransientResource &resource, const char *render_pass_name) {
	VkSampleCountFlagBits max_multisample_count = VkUtils::GetMaxMultisampleCount(
		context.gpu.properties.properties.limits.framebufferColorSampleCounts,
		context.gpu.properties.properties.limits.framebufferDepthSampleCounts
	);

	if(!strcmp(resource.name, "RENDER_OUTPUT")) {
		assert(resource.type == TransientResourceType::Image);
		// If the render output is multisampled, create MSAA image to resolve from
		if(resource.image.multisampled) {
			std::string msaa_image_name = std::string(render_pass_name) + "_MSAA";
			images[msaa_image_name] = resource_manager.Create2DImage(
				context.swapchain.extent.width,
				context.swapchain.extent.height, 
				context.swapchain.format, 
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				max_multisample_count
			);
			image_access[msaa_image_name] = ImageAccess {
				.layout = VK_IMAGE_LAYOUT_UNDEFINED,
				.access_flags = 0,
				.stage_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
			};
			resource_manager.TagImage(images[msaa_image_name], msaa_image_name.c_str());
		}

		return;
	}
	
	if(!images.contains(resource.name)) {
		VkImageUsageFlags usage = VkUtils::IsDepthFormat(resource.image.format) ?
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | 
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT :
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | 
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		// Swapchain-sized image
		if(resource.image.width == 0 && resource.image.height == 0) {
			images[resource.name] = resource_manager.Create2DImage(context.swapchain.extent.width,
				context.swapchain.extent.height, resource.image.format, usage, VK_IMAGE_LAYOUT_GENERAL,
				resource.image.multisampled ? max_multisample_count : VK_SAMPLE_COUNT_1_BIT);
		}
		else {
			images[resource.name] = resource_manager.Create2DImage(resource.image.width,
				resource.image.height, resource.image.format, usage, VK_IMAGE_LAYOUT_GENERAL,
				resource.image.multisampled ? max_multisample_count : VK_SAMPLE_COUNT_1_BIT);
		}
		image_access[resource.name] = ImageAccess {
			.layout = VK_IMAGE_LAYOUT_GENERAL,
			.access_flags = 0,
			.stage_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
		};
		resource_manager.TagImage(images[resource.name], resource.name);
	}
}

// Sanity check: Resources should remain the same width height and format during execution
bool RenderGraph::SanityCheck() {
	std::unordered_map<std::string, std::vector<TransientResource>> participating_resources;
	for(std::string &pass_name : execution_order) {
		RenderPassDescription &pass = pass_descriptions[pass_name];
		for(TransientResource &dependency : pass.dependencies) {
			participating_resources[dependency.name].emplace_back(dependency);
		}
		for(TransientResource &output : pass.outputs) {
			participating_resources[output.name].emplace_back(output);
		}
	}

	for(auto &[name, resources] : participating_resources) {
		if(!strcmp(name.c_str(), "RENDER_OUTPUT")) {
			continue;
		}

		if(resources.empty()) {
			return false;
		}

		if(resources.front().type == TransientResourceType::Image) {
			uint32_t width = resources.front().image.width;
			uint32_t height = resources.front().image.height;
			VkFormat format = resources.front().image.format;

			for(TransientResource &resource : resources) {
				if(resource.image.width != width ||
					resource.image.height != height ||
					resource.image.format != format) {
					return false;
				}
			}

		}
		else if(resources.front().type == TransientResourceType::Buffer) {
			// TODO: Buffers
			assert(false);
		}
	}
	return true;
}

