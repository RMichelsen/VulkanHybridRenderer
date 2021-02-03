#include "pch.h"
#include "render_graph.h"

#include "pipeline.h"
#include "resource_manager.h"
#include "vulkan_context.h"
#include "vulkan_utils.h"

RenderGraph::RenderGraph(VulkanContext &context) : context(context) {}

// Adds a new graphics render pass. Inputs are allocated in a new descriptor set in the order in which they 
// appear in the vector. The global descriptor set resides at slot 0, thus user-defined descriptors are allocated
// in set 1.
void RenderGraph::AddGraphicsPass(const char *render_pass_name, std::vector<TransientResource> inputs, 
	std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines, 
	RenderPassCallback callback) {

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
		.pipeline_descriptions = pipelines,
		.callback = callback
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

	std::unordered_map<std::string, VkImageLayout> current_layouts;

	for(std::string &pass_name : execution_order) {
		RenderPassDescription &pass = layout.render_pass_descriptions[pass_name];
		RenderPass render_pass {
			.callback = pass.callback
		};

		std::vector<VkDescriptorSetLayoutBinding> bindings;
		std::vector<VkDescriptorImageInfo> descriptors;
		for(uint32_t i = 0; i < pass.inputs.size(); ++i) {
			std::string &input = pass.inputs[i];
			// TODO: Add Buffers
			assert(layout.transient_resources[input].type == TransientResourceType::Texture);

			if(VkUtils::IsDepthFormat(layout.transient_resources[input].texture.format)) {
				continue;
			}

			bindings.emplace_back(VkDescriptorSetLayoutBinding {
				.binding = i,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT // TODO: PARAM
			});
			descriptors.emplace_back(VkDescriptorImageInfo {
				.sampler = resource_manager.sampler,
				.imageView = textures[input].image_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			});
		}
		if(!pass.inputs.empty()) {
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
			VkWriteDescriptorSet write_descriptor_set {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = render_pass.descriptor_set,
				.dstBinding = 0,
				.descriptorCount = static_cast<uint32_t>(descriptors.size()),
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = descriptors.data()
			};
			vkUpdateDescriptorSets(context.device, 1, &write_descriptor_set, 0, nullptr);
		}

		VkSubpassDescription subpass_description {
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		};
		std::vector<VkAttachmentDescription> attachments;
		std::vector<VkAttachmentReference> color_attachment_refs;
		for(std::string &output : pass.outputs) {
			bool is_backbuffer = output == "BACKBUFFER";

			if(!current_layouts.contains(output)) {
				current_layouts[output] = VK_IMAGE_LAYOUT_UNDEFINED;
			}

			TransientResource &resource = layout.transient_resources[output];
			// TODO: Add Buffers
			assert(resource.type == TransientResourceType::Texture);
			render_pass.attachments.emplace_back(resource);

			if(VkUtils::IsDepthFormat(resource.texture.format)) {
				VkAttachmentDescription attachment {
					.format = resource.texture.format,
					.samples = VK_SAMPLE_COUNT_1_BIT,
					.loadOp = current_layouts[output] == VK_IMAGE_LAYOUT_UNDEFINED ?
						VK_ATTACHMENT_LOAD_OP_CLEAR :
						VK_ATTACHMENT_LOAD_OP_LOAD,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
					.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
					.initialLayout = current_layouts[output],
					.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
				};
				VkAttachmentReference attachment_ref {
					.attachment = static_cast<uint32_t>(attachments.size()),
					.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
				};
				attachments.emplace_back(attachment);
				subpass_description.pDepthStencilAttachment = &attachment_ref;
			}
			else {
				VkAttachmentDescription attachment {
					.format = is_backbuffer ?
						context.swapchain.format :
						layout.transient_resources[output].texture.format,
					.samples = VK_SAMPLE_COUNT_1_BIT,
					.loadOp = current_layouts[output] == VK_IMAGE_LAYOUT_UNDEFINED ?
						VK_ATTACHMENT_LOAD_OP_CLEAR :
						VK_ATTACHMENT_LOAD_OP_LOAD,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
					.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
					.initialLayout = current_layouts[output],
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

		VK_CHECK(vkCreateRenderPass(context.device, &render_pass_info, nullptr, &render_pass.handle));

		for(GraphicsPipelineDescription &pipeline_description : pass.pipeline_descriptions) {
			assert(!graphics_pipelines.contains(pipeline_description.name));

			graphics_pipelines[pipeline_description.name] = VkUtils::CreateGraphicsPipeline(context,
				resource_manager, render_pass, pipeline_description);
		}

		assert(!render_passes.contains(pass_name));
		render_passes[pass_name] = render_pass;
	}
}

void RenderGraph::Execute(ResourceManager &resource_manager, VkCommandBuffer &command_buffer, 
	uint32_t resource_idx, uint32_t image_idx) {
	for(std::string &pass_name : execution_order) {
		assert(render_passes.contains(pass_name));
		RenderPass &render_pass = render_passes[pass_name];
		VkFramebuffer &framebuffer = render_pass.framebuffers[resource_idx];

		// Delete previous framebuffer
		if(framebuffer != VK_NULL_HANDLE) {
			vkDestroyFramebuffer(context.device, framebuffer, nullptr);
			framebuffer = VK_NULL_HANDLE;
		}

		std::vector<VkImageView> image_views;
		std::vector<VkClearValue> clear_values;
		for(TransientResource &attachment : render_pass.attachments) {
			if(attachment.name == "BACKBUFFER") {
				image_views.emplace_back(context.swapchain.image_views[image_idx]);
			}
			else {
				image_views.emplace_back(textures[attachment.name].image_view);
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
			.renderPass = render_pass.handle,
			.attachmentCount = static_cast<uint32_t>(image_views.size()),
			.pAttachments = image_views.data(),
			.width = context.swapchain.extent.width,
			.height = context.swapchain.extent.height,
			.layers = 1
		};

		VK_CHECK(vkCreateFramebuffer(context.device, &framebuffer_info, nullptr, &framebuffer));
	
		VkRenderPassBeginInfo render_pass_begin_info {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = render_pass.handle,
			.framebuffer = framebuffer,
			.renderArea = VkRect2D {
				.offset = VkOffset2D { .x = 0, .y = 0 },
				.extent = context.swapchain.extent
			},
			.clearValueCount = static_cast<uint32_t>(clear_values.size()),
			.pClearValues = clear_values.data()
		};

		vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

		render_pass.callback(
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
	// TODO: Add Buffers
	assert(resource.type == TransientResourceType::Texture);

	if(!textures.contains(resource.name) && resource.name != "BACKBUFFER") {
		textures[resource.name] = resource_manager.CreateTransientTexture(
			resource.texture.width, resource.texture.height, resource.texture.format);
	}
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
