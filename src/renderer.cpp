#include "pch.h"
#include "renderer.h"

#include "pipeline.h"
#include "render_graph.h"
#include "resource_manager.h"
#include "scene_loader.h"
#include "vulkan_context.h"

#include <chrono>

Renderer::Renderer(HINSTANCE hinstance, HWND hwnd) {
	auto t1 = std::chrono::high_resolution_clock::now();

	context = std::make_unique<VulkanContext>(hinstance, hwnd);
	resource_manager = std::make_unique<ResourceManager>(*context);
	render_graph = std::make_unique<RenderGraph>(*context);
	scene = SceneLoader::LoadScene(*resource_manager, "data/models/Sponza.glb");
	CreatePipeline();

	auto t2 = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
	printf("%lld\n", duration);
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
		.pCommandBuffers = &resources.command_buffer,
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
	resource_manager->UpdatePerFrameUBO(resource_idx, 
		PerFrameData {
			.camera_view = scene.camera.view,
			.camera_proj = scene.camera.perspective
		}
	);

	VkCommandBufferBeginInfo command_buffer_begin_info {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	VK_CHECK(vkBeginCommandBuffer(resources.command_buffer, &command_buffer_begin_info));

	render_graph->Execute(*resource_manager, resources.command_buffer, resource_idx, image_idx);

	VK_CHECK(vkEndCommandBuffer(resources.command_buffer));
}

void Renderer::CreatePipeline() {
	render_graph->AddGraphicsPass("G-Buffer",
		{},
		{
			CreateTransientTexture("Position", VK_FORMAT_R16G16B16A16_SFLOAT),
			CreateTransientTexture("Normal", VK_FORMAT_R16G16B16A16_SFLOAT),
			CreateTransientTexture("Albedo", VK_FORMAT_R8G8B8A8_UNORM),
			CreateTransientTexture("Depth", VK_FORMAT_D32_SFLOAT)
		},
		{
			GraphicsPipelineDescription {
				.name = "G-Buffer Pipeline",
				.vertex_shader = "data/shaders/compiled/gbuf.vert.spv",
				.fragment_shader = "data/shaders/compiled/gbuf.frag.spv",
				.vertex_input_state = VertexInputState::Default,
				.rasterization_state = RasterizationState::Fill,
				.multisample_state = MultisampleState::Off,
				.depth_stencil_state = DepthStencilState::On,
				.push_constants = PushConstantDescription {
					.size = sizeof(PushConstants),
					.pipeline_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
				}
			}
		},
		[this](ExecutePipelineCallback execute_pipeline) {
			execute_pipeline("G-Buffer Pipeline",
				[this](GraphicsPipelineExecutionContext &context) {
					context.BindGlobalVertexAndIndexBuffers();
					for(Mesh &mesh : scene.meshes) {
						for(Primitive &primitive : mesh.primitives) {
							PushConstants push_constants {
								.transform = primitive.transform,
								.texture = primitive.texture
							};
							context.PushConstants(push_constants);
							context.DrawIndexed(primitive.index_count, 1, primitive.index_offset, primitive.vertex_offset, 0);
						}
					}
				}
			);
		}
	);

	render_graph->AddGraphicsPass("Composition Pass", 
		{
			CreateTransientTexture("Position", VK_FORMAT_R16G16B16A16_SFLOAT),
			CreateTransientTexture("Normal", VK_FORMAT_R16G16B16A16_SFLOAT),
			CreateTransientTexture("Albedo", VK_FORMAT_R8G8B8A8_UNORM)
		},
		{
			TRANSIENT_BACKBUFFER,
			CreateTransientTexture("Depth", VK_FORMAT_D32_SFLOAT)
		},
		{
			GraphicsPipelineDescription {
				.name = "Composition Pipeline",
				.vertex_shader = "data/shaders/compiled/composition.vert.spv",
				.fragment_shader = "data/shaders/compiled/composition.frag.spv",
				.vertex_input_state = VertexInputState::Empty,
				.rasterization_state = RasterizationState::FillCullCCW,
				.multisample_state = MultisampleState::Off,
				.depth_stencil_state = DepthStencilState::On,
				.push_constants = PUSHCONSTANTS_NONE
			}
		},
		[](ExecutePipelineCallback execute_pipeline) {
			execute_pipeline("Composition Pipeline",
				[](GraphicsPipelineExecutionContext &context) {
					context.Draw(3, 1, 0, 0);
				}
			);
		}
	);

	render_graph->Compile(*resource_manager);
}

TransientResource Renderer::CreateTransientTexture(const char *name, VkFormat format) {
	return TransientResource {
		.name = name,
		.type = TransientResourceType::Texture,
		.texture = TransientTexture {
			.format = format,
			.width = context->swapchain.extent.width,
			.height = context->swapchain.extent.height,
			.color_blending = false
		}
	};
}

TransientResource Renderer::CreateTransientTexture(const char *name, uint32_t width, uint32_t height, VkFormat format) {
	return TransientResource {
		.name = name,
		.type = TransientResourceType::Texture,
		.texture = TransientTexture {
			.format = format,
			.width = width,
			.height = height,
			.color_blending = false
		}
	};
}
