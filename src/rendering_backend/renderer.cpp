#include "pch.h"
#include "renderer.h"

#include "rendering_backend/pipeline.h"
#include "rendering_backend/resource_manager.h"
#include "rendering_backend/user_interface.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"
#include "render_graph/render_graph.h"
#include "render_graph/graphics_execution_context.h"
#include "render_graph/raytracing_execution_context.h"
#include "render_paths/hybrid_render_path.h"
#include "render_paths/rayquery_render_path.h"
#include "render_paths/raytraced_render_path.h"
#include "scene/scene_loader.h"

Renderer::Renderer(HINSTANCE hinstance, HWND hwnd) : context(std::make_unique<VulkanContext>(hinstance, hwnd)) {
	resource_manager = std::make_unique<ResourceManager>(*context);
	render_graph = std::make_unique<RenderGraph>(*context, *resource_manager);
	user_interface = std::make_unique<UserInterface>(*context, *resource_manager);
	resource_manager->LoadScene("Sponza_WithLight.glb");

	active_render_path = std::make_unique<HybridRenderPath>(*context, *render_graph, *resource_manager);
	active_render_path->Build();

	user_interface_state = {
		.render_path_state = RenderPathState::Idle,
		.debug_texture = ""
	};
}

Renderer::~Renderer() {
	user_interface->DestroyResources();
	render_graph->DestroyResources();
	resource_manager->DestroyResources();
	context->DestroyResources();
}

void Renderer::Update() {
	user_interface_state = user_interface->Update(*active_render_path, render_graph->GetColorAttachments());
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
		user_interface->ResizeToSwapchain();
		active_render_path->Build();
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
			user_interface->ResizeToSwapchain();
			active_render_path->Build();
			return;
		}
	}
	VK_CHECK(result);

	resource_idx = (resource_idx + 1) % MAX_FRAMES_IN_FLIGHT;

	switch(user_interface_state.render_path_state) {
	case RenderPathState::ChangeToHybrid: {
		active_render_path = std::make_unique<HybridRenderPath>(*context, *render_graph, *resource_manager);
		active_render_path->Build();
	} break;
	case RenderPathState::ChangeToRayquery: {
		active_render_path = std::make_unique<RayqueryRenderPath>(*context, *render_graph, *resource_manager);
		active_render_path->Build();
	} break;
	case RenderPathState::ChangeToRaytraced: {
		active_render_path = std::make_unique<RaytracedRenderPath>(*context, *render_graph, *resource_manager);
		active_render_path->Build();
	} break;
	case RenderPathState::Idle:
	default: {
	}
	}
}

void Renderer::Render(FrameResources &resources, uint32_t resource_idx, uint32_t image_idx) {
	resource_manager->UpdatePerFrameUBO(resource_idx, 
		PerFrameData {
			.camera_view = resource_manager->scene.camera.view,
			.camera_proj = resource_manager->scene.camera.perspective,
			.camera_view_inverse = glm::inverse(resource_manager->scene.camera.view),
			.camera_proj_inverse = glm::inverse(resource_manager->scene.camera.perspective),
			.directional_light = resource_manager->scene.directional_light,
		}
	);

	VkCommandBufferBeginInfo command_buffer_begin_info {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	VK_CHECK(vkBeginCommandBuffer(resources.command_buffer, &command_buffer_begin_info));
	
	render_graph->Execute(resources.command_buffer, resource_idx, image_idx);

	if(!user_interface_state.debug_texture.empty()) {
		VkFormat format = render_graph->GetImageFormat(user_interface_state.debug_texture);
		uint32_t active_debug_texture = user_interface->SetActiveDebugTexture(format);
		render_graph->CopyImage(
			resources.command_buffer,
			user_interface_state.debug_texture,
			resource_manager->textures[active_debug_texture]
		);
	}

	VkDebugUtilsLabelEXT pass_label {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		.pLabelName = "User Interface Pass"
	};
	vkCmdBeginDebugUtilsLabelEXT(resources.command_buffer, &pass_label);
	user_interface->Draw(*resource_manager, resources.command_buffer, resource_idx, image_idx);
	vkCmdEndDebugUtilsLabelEXT(resources.command_buffer);

	VK_CHECK(vkEndCommandBuffer(resources.command_buffer));
}

