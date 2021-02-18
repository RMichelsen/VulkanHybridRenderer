#include "pch.h"
#include "renderer.h"

#include "graphics_execution_context.h"
#include "pipeline.h"
#include "raytracing_execution_context.h"
#include "render_graph.h"
#include "resource_manager.h"
#include "scene_loader.h"
#include "user_interface.h"
#include "vulkan_context.h"
#include "vulkan_utils.h"

Renderer::Renderer(HINSTANCE hinstance, HWND hwnd) : context(std::make_unique<VulkanContext>(hinstance, hwnd)) {
	resource_manager = std::make_unique<ResourceManager>(*context);
	render_graph = std::make_unique<RenderGraph>(*context, *resource_manager);
	user_interface = std::make_unique<UserInterface>(*context, *resource_manager);
	scene = SceneLoader::LoadScene(*resource_manager, "data/models/Sponza_WithLight.glb");

	render_graph->AddGraphicsPass("G-Buffer Pass",
		{},
		{
			VkUtils::CreateTransientAttachmentImage("Position", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
			VkUtils::CreateTransientAttachmentImage("Normal", VK_FORMAT_R16G16B16A16_SFLOAT, 1),
			VkUtils::CreateTransientAttachmentImage("Albedo", VK_FORMAT_B8G8R8A8_UNORM, 2),
			VkUtils::CreateTransientAttachmentImage("Depth", VK_FORMAT_D32_SFLOAT, 3)
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
				.dynamic_state = DynamicState::None,
				.push_constants = PushConstantDescription {
					.size = sizeof(PushConstants),
					.pipeline_stage = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
				}
			}
		},
		[this](ExecuteGraphicsCallback execute_pipeline) {
			execute_pipeline("G-Buffer Pipeline",
				[this](GraphicsExecutionContext &execution_context) {
					execution_context.BindGlobalVertexAndIndexBuffers();
					for(Mesh &mesh : scene.meshes) {
						for(int i = 0; i < mesh.primitives.size(); ++i) {
							Primitive &primitive = mesh.primitives[i];
							PushConstants push_constants {
								.object_id = i
							};
							execution_context.PushConstants(push_constants);
							execution_context.DrawIndexed(primitive.index_count, 1, primitive.index_offset,
								primitive.vertex_offset, 0);
						}
					}
				}
			);
		}
	);

	render_graph->AddRaytracingPass("Raytracing Pass",
		{},
		{
			VkUtils::CreateTransientStorageImage("Rays", VK_FORMAT_B8G8R8A8_UNORM, 0)
		},
		RaytracingPipelineDescription {
			.name = "Raytracing Pipeline",
			.raygen_shader = "data/shaders/compiled/raygen.rgen.spv",
			.hit_shader = "data/shaders/compiled/closesthit.rchit.spv",
			.miss_shader = "data/shaders/compiled/miss.rmiss.spv",
			.shadow_miss_shader = "data/shaders/compiled/shadow_miss.rmiss.spv"
		},
		[this](ExecuteRaytracingCallback execute_pipeline) {
			execute_pipeline("Raytracing Pipeline",
				[this](RaytracingExecutionContext &execution_context) {
					execution_context.TraceRays(context->swapchain.extent.width,
						context->swapchain.extent.height);
				}
			);
		}
	);

	render_graph->AddGraphicsPass("Composition Pass",
		{
			VkUtils::CreateTransientSampledImage("Position", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
			VkUtils::CreateTransientSampledImage("Normal", VK_FORMAT_R16G16B16A16_SFLOAT, 1),
			VkUtils::CreateTransientSampledImage("Albedo", VK_FORMAT_B8G8R8A8_UNORM, 2),
			VkUtils::CreateTransientSampledImage("Rays", VK_FORMAT_B8G8R8A8_UNORM, 3)
		},
		{
			VkUtils::CreateTransientBackbuffer(0, ColorBlendState::ImGui),
			VkUtils::CreateTransientAttachmentImage("Depth", VK_FORMAT_D32_SFLOAT, 1),
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
				.dynamic_state = DynamicState::None,
				.push_constants = PUSHCONSTANTS_NONE
			},
			IMGUI_PIPELINE_DESCRIPTION
		},
		[this](ExecuteGraphicsCallback execute_pipeline) {
			execute_pipeline("Composition Pipeline",
				[](GraphicsExecutionContext &execution_context) {
					execution_context.Draw(3, 1, 0, 0);
				}
			);
			execute_pipeline("ImGui Pipeline",
				[this](GraphicsExecutionContext &execution_context) {
					user_interface->Draw(execution_context);
				}
			);
		}
	);

	render_graph->Build();
	anchor = 0.5f;
}

Renderer::~Renderer() {
	user_interface->DestroyResources();
	render_graph->DestroyResources();
	resource_manager->DestroyResources();
	context->DestroyResources();
}

void Renderer::Update() {
	user_interface->Update();
}

void Renderer::Present(HWND hwnd) {
	static uint32_t resource_idx = 0;
	FrameResources &resources = context->frame_resources[resource_idx];

	VK_CHECK(vkWaitForFences(context->device, 1, &resources.fence, VK_TRUE, UINT64_MAX));
	VK_CHECK(vkResetFences(context->device, 1, &resources.fence));

	uint32_t image_idx;
	VkResult result = vkAcquireNextImageKHR(context->device, context->swapchain.handle, 
		UINT64_MAX, resources.image_available, VK_NULL_HANDLE, &image_idx);
	if(result == VK_ERROR_OUT_OF_DATE_KHR) {
		context->Resize(hwnd);
		render_graph->Build();
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
	if(!((result == VK_SUCCESS) || (result == VK_SUBOPTIMAL_KHR))) {
		if(result == VK_ERROR_OUT_OF_DATE_KHR) {
			context->Resize(hwnd);
			render_graph->Build();
			return;
		}
	}
	VK_CHECK(result);

	resource_idx = (resource_idx + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::SetAnchor(float mouse_xpos) {
	anchor = mouse_xpos / static_cast<float>(context->swapchain.extent.width);
}

bool Renderer::PosOnAnchor(float mouse_xpos) {
	float target_anchor = mouse_xpos / static_cast<float>(context->swapchain.extent.width);
	if(fabs(anchor - target_anchor) < 0.002f) {
		return true;
	}
	return false;
}

void Renderer::Render(FrameResources &resources, uint32_t resource_idx, uint32_t image_idx) {
	resource_manager->UpdatePerFrameUBO(resource_idx, 
		PerFrameData {
			.camera_view = scene.camera.view,
			.camera_proj = scene.camera.perspective,
			.camera_view_inverse = glm::inverse(scene.camera.view),
			.camera_proj_inverse = glm::inverse(scene.camera.perspective),
			.directional_light = scene.directional_light,
			.anchor = anchor
		}
	);

	VkCommandBufferBeginInfo command_buffer_begin_info {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	VK_CHECK(vkBeginCommandBuffer(resources.command_buffer, &command_buffer_begin_info));
	
	render_graph->Execute(resources.command_buffer, resource_idx, image_idx);

	VK_CHECK(vkEndCommandBuffer(resources.command_buffer));
}

