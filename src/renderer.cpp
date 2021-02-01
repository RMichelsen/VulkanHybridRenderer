#include "pch.h"
#include "renderer.h"

#include "pipeline.h"
#include "render_graph.h"
#include "resource_manager.h"
#include "scene_loader.h"
#include "shader_compiler.h"
#include "vulkan_context.h"

Renderer::Renderer(HINSTANCE hinstance, HWND hwnd) {
	context = std::make_unique<VulkanContext>(hinstance, hwnd);
	resource_manager = std::make_unique<ResourceManager>(*context);
	render_graph = std::make_unique<RenderGraph>(*context);
	//scene = SceneLoader::LoadScene(*resource_manager, "data/models/Sponza/glTF/Sponza.gltf");
	scene = SceneLoader::LoadScene(*resource_manager, "data/models/Sponza.glb");
	CreatePipeline();
}

Renderer::~Renderer() {
	resource_manager.reset();
	context.reset();
}

void Renderer::Update() {

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

	Render(resources, resource_idx, image_idx);

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

void Renderer::Render(FrameResources &resources, uint32_t resource_idx, uint32_t image_idx) {
	VkCommandBufferBeginInfo commandbuffer_begin_info {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	VK_CHECK(vkBeginCommandBuffer(resources.commandbuffer, &commandbuffer_begin_info));

	render_graph->Execute(resources.commandbuffer, resource_idx, image_idx);

	VK_CHECK(vkEndCommandBuffer(resources.commandbuffer));

	return;

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
		pipeline.handle);

	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(resources.commandbuffer, 0, 1, 
		&resource_manager->global_vertex_buffer.handle, &offset);
	vkCmdBindIndexBuffer(resources.commandbuffer, 
		resource_manager->global_index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
	vkCmdBindDescriptorSets(resources.commandbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline.layout, 0, 1, &resource_manager->texture_array_descriptor_set, 0, nullptr);

	for(Mesh &mesh : scene.meshes) {
		for(Primitive &primitive : mesh.primitives) {
			PushConstants push_constants {
				.MVP = scene.camera.perspective * scene.camera.view * primitive.transform,
				.texture = primitive.texture
			};
			vkCmdPushConstants(resources.commandbuffer, pipeline.layout, 
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
	GraphicsPipelineDescription pipeline_description {
		.vertex_shader = "data/shaders/default.vert",
		.fragment_shader = "data/shaders/default.frag",
		.rasterization_state = RasterizationState::Fill,
		.multisample_state = MultisampleState::Off,
		.depth_stencil_state = DepthStencilState::On,
		.color_blend_states = { ColorBlendState::Off }
	};

	pipeline = VkUtils::CreateGraphicsPipeline(
		*context, 
		*resource_manager, 
		RenderPass {
			.handle = context->render_pass,
			.descriptor_set_layout = VK_NULL_HANDLE,
			.descriptor_set = VK_NULL_HANDLE
		},
		pipeline_description
	);

	render_graph->AddGraphicsPass("G-Buffer",
		{},
		{
			TransientResource {
				.name = "Position",
				.type = TransientResourceType::Texture,
				.texture = TransientTexture {
					.format = VK_FORMAT_R8G8B8A8_UNORM,
					.width = context->swapchain.extent.width,
					.height = context->swapchain.extent.height
				}
			},
			TransientResource {
				.name = "Normal",
				.type = TransientResourceType::Texture,
				.texture = TransientTexture {
					.format = VK_FORMAT_R8G8B8A8_UNORM,
					.width = context->swapchain.extent.width,
					.height = context->swapchain.extent.height
				}
			},
			TransientResource {
				.name = "Albedo",
				.type = TransientResourceType::Texture,
				.texture = TransientTexture {
					.format = VK_FORMAT_R8G8B8A8_UNORM,
					.width = context->swapchain.extent.width,
					.height = context->swapchain.extent.height
				}
			},
			TransientResource {
				.name = "Depth",
				.type = TransientResourceType::Texture,
				.texture = TransientTexture {
					.format = VK_FORMAT_D32_SFLOAT,
					.width = context->swapchain.extent.width,
					.height = context->swapchain.extent.height
				}
			}
		},
		{
			GraphicsPipelineDescription {
				.name = "G-Buffer Pipeline",
				.vertex_shader = "data/shaders/gbuf.vert",
				.fragment_shader = "data/shaders/gbuf.frag",
				.rasterization_state = RasterizationState::Fill,
				.multisample_state = MultisampleState::Off,
				.depth_stencil_state = DepthStencilState::On,
				.color_blend_states = { 
					ColorBlendState::Off, 
					ColorBlendState::Off, 
					ColorBlendState::Off 
				}
			}
		},
		[&](RenderPass &render_pass, std::unordered_map<std::string, GraphicsPipeline> &pipelines,
			VkCommandBuffer &command_buffer) {
			GraphicsPipeline &pipeline_ = pipelines["G-Buffer Pipeline"];
			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.handle);

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(command_buffer, 0, 1, &resource_manager->global_vertex_buffer.handle, &offset);
			vkCmdBindIndexBuffer(command_buffer, resource_manager->global_index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.layout, 
				0, 1, &resource_manager->texture_array_descriptor_set, 0, nullptr);

			for(Mesh &mesh : scene.meshes) {
				for(Primitive &primitive : mesh.primitives) {
					PushConstants push_constants {
						.MVP = scene.camera.perspective * scene.camera.view * primitive.transform,
						.texture = primitive.texture
					};
					vkCmdPushConstants(command_buffer, pipeline_.layout, 
						VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 
						sizeof(PushConstants), &push_constants);

					vkCmdDrawIndexed(command_buffer, primitive.index_count,
						1, primitive.index_offset, primitive.vertex_offset, 0);
				}
			}
		}
	);

	render_graph->AddGraphicsPass("Composition Pass",
		{
			TransientResource {
				.name = "Position",
				.type = TransientResourceType::Texture,
				.texture = TransientTexture {
					.format = VK_FORMAT_R8G8B8A8_UNORM,
					.width = context->swapchain.extent.width,
					.height = context->swapchain.extent.height
				}
			},
			TransientResource {
				.name = "Normal",
				.type = TransientResourceType::Texture,
				.texture = TransientTexture {
					.format = VK_FORMAT_R8G8B8A8_UNORM,
					.width = context->swapchain.extent.width,
					.height = context->swapchain.extent.height
				}
			},
			TransientResource {
				.name = "Albedo",
				.type = TransientResourceType::Texture,
				.texture = TransientTexture {
					.format = VK_FORMAT_R8G8B8A8_UNORM,
					.width = context->swapchain.extent.width,
					.height = context->swapchain.extent.height
				}
			}
		},
		{
			TRANSIENT_BACKBUFFER,
			TransientResource {
				.name = "Depth",
				.type = TransientResourceType::Texture,
				.texture = TransientTexture {
					.format = VK_FORMAT_D32_SFLOAT,
					.width = context->swapchain.extent.width,
					.height = context->swapchain.extent.height
				}
			}
		},
		{
			GraphicsPipelineDescription {
				.name = "Composition Pipeline",
				.vertex_shader = "data/shaders/composition.vert",
				.fragment_shader = "data/shaders/composition.frag",
				.rasterization_state = RasterizationState::Fill,
				.multisample_state = MultisampleState::Off,
				.depth_stencil_state = DepthStencilState::On,
				.color_blend_states = { ColorBlendState::Off }
			}
		},
		[&](RenderPass &render_pass, std::unordered_map<std::string, GraphicsPipeline> &pipelines,
			VkCommandBuffer &command_buffer) {
			GraphicsPipeline &pipeline_ = pipelines["Composition Pipeline"];
			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.handle);

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(command_buffer, 0, 1, &resource_manager->global_vertex_buffer.handle, &offset);
			vkCmdBindIndexBuffer(command_buffer, resource_manager->global_index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.layout, 
				0, 1, &resource_manager->texture_array_descriptor_set, 0, nullptr);
			vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.layout, 
				1, 1, &render_pass.descriptor_set, 0, nullptr);

			for(Mesh &mesh : scene.meshes) {
				for(Primitive &primitive : mesh.primitives) {
					PushConstants push_constants {
						.MVP = scene.camera.perspective * scene.camera.view * primitive.transform,
						.texture = primitive.texture
					};
					vkCmdPushConstants(command_buffer, pipeline_.layout, 
						VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 
						sizeof(PushConstants), &push_constants);

					vkCmdDrawIndexed(command_buffer, primitive.index_count,
						1, primitive.index_offset, primitive.vertex_offset, 0);
				}
			}
		}
	);

	render_graph->Compile(*resource_manager);
}
