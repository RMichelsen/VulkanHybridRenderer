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
#include "render_paths/forward_raster_render_path.h"
#include "render_paths/hybrid_render_path.h"
#include "render_paths/rayquery_render_path.h"
#include "render_paths/raytraced_render_path.h"
#include "scene/scene_loader.h"

Renderer::Renderer(HINSTANCE hinstance, HWND hwnd) : context(std::make_unique<VulkanContext>(hinstance, hwnd)) {
	resource_manager = std::make_unique<ResourceManager>(*context);
	render_graph = std::make_unique<RenderGraph>(*context, *resource_manager);
	user_interface = std::make_unique<UserInterface>(*context, *resource_manager);
	resource_manager->LoadScene("Sponza.glb");

	active_render_path = std::make_unique<HybridRenderPath>(*context, *render_graph, *resource_manager);
	active_render_path->Build();

	user_interface_state = {
		.render_path_state = RenderPathState::Idle,
		.debug_texture = ""
	};

	int _, x, y;
	uint8_t *image_data;
	image_data = stbi_load("data/misc/blue_noise/LDR_RGBA_0.png", &x, &y, &_, STBI_rgb_alpha);
	blue_noise_texture_index = resource_manager->UploadTextureFromData(x, y, image_data);
	free(image_data);
}

Renderer::~Renderer() {
	user_interface->DestroyResources();
	render_graph->DestroyResources();
	resource_manager->DestroyResources();
	context->DestroyResources();
}

void Renderer::Update() {
	user_interface_state = user_interface->Update(*render_graph, *active_render_path, render_graph->GetColorAttachments());

	ImGuiIO &io = ImGui::GetIO();
	Camera &camera = resource_manager->scene.camera;
	float camera_speed = 0.75f;
	float movement_speed = 10.0f;

	bool w_down = false;
	bool a_down = false;
	bool s_down = false;
	bool d_down = false;
	if(context->hwnd == GetFocus()) {
		w_down = (GetKeyState(static_cast<int>('W')) & 0x8000);
		a_down = (GetKeyState(static_cast<int>('A')) & 0x8000);
		s_down = (GetKeyState(static_cast<int>('S')) & 0x8000);
		d_down = (GetKeyState(static_cast<int>('D')) & 0x8000);
	}
	if(w_down || a_down || s_down || d_down) {
		glm::vec3 forward = glm::normalize(glm::vec3(glm::row(camera.view, 2)));
		glm::vec3 position = glm::vec3(glm::column(camera.transform, 3));

		glm::vec3 new_position = position;
		if(w_down) {
			new_position -= (forward * movement_speed * io.DeltaTime);
		}
		if(s_down) {
			new_position += (forward * movement_speed * io.DeltaTime);
		}
		if(a_down) {
			new_position += (glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)) * movement_speed * io.DeltaTime);
		}
		if(d_down) {
			new_position -= (glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)) * movement_speed * io.DeltaTime);
		}

		camera.transform[3][0] = new_position.x;
		camera.transform[3][1] = new_position.y;
		camera.transform[3][2] = new_position.z;
		camera.view = glm::inverse(camera.transform);
	}
	if(io.MouseDown[1] && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
		camera.yaw -= io.MouseDelta.x * camera_speed * io.DeltaTime;
		camera.pitch -= io.MouseDelta.y * camera_speed * io.DeltaTime;
		if(camera.pitch > 1.55f) {
			camera.pitch = 1.55f;
		}
		if(camera.pitch < -1.55f) {
			camera.pitch = -1.55f;
		}
		glm::mat4 R = glm::yawPitchRoll(camera.yaw, camera.pitch, camera.roll);
		glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(glm::column(camera.transform, 3)));
		camera.transform = T * R;
		camera.view = glm::inverse(camera.transform);
	}
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
		context->Resize();
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
			context->Resize();
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
	} break;
	case RenderPathState::ChangeToRayquery: {
		active_render_path = std::make_unique<RayqueryRenderPath>(*context, *render_graph, *resource_manager);
	} break;
	case RenderPathState::ChangeToRaytraced: {
		active_render_path = std::make_unique<RaytracedRenderPath>(*context, *render_graph, *resource_manager);
	} break;
	case RenderPathState::ChangeToForwardRaster: {
		active_render_path = std::make_unique<ForwardRasterRenderPath>(*context, *render_graph, *resource_manager);
	} break;
	case RenderPathState::Idle:
	default: {
	}
	}

	render_graph->GatherPerformanceStatistics();
}

void Renderer::Render(FrameResources &resources, uint32_t resource_idx, uint32_t image_idx) {
	Camera &camera = resource_manager->scene.camera;

	static uint32_t frame_index = 0;
	static PerFrameData per_frame_data {};
	glm::mat4 prev_frame_view = per_frame_data.camera_view;
	glm::mat4 prev_frame_proj = per_frame_data.camera_proj;
	per_frame_data = PerFrameData {
		.camera_view = camera.view,
		.camera_proj = camera.perspective,
		.camera_view_inverse = camera.transform,
		.camera_proj_inverse = glm::inverse(camera.perspective),
		.camera_viewproj_inverse = glm::inverse(camera.perspective * camera.view),
		.camera_view_prev_frame = prev_frame_view,
		.camera_proj_prev_frame = prev_frame_proj,
		.directional_light = resource_manager->scene.directional_light,
		.display_size = { context->swapchain.extent.width, context->swapchain.extent.height },
		.display_size_inverse = { 1.0f / context->swapchain.extent.width, 1.0f / context->swapchain.extent.height },
		.frame_index = frame_index++,
		.blue_noise_texture_index = blue_noise_texture_index,
	};
	resource_manager->UpdatePerFrameUBO(resource_idx, per_frame_data);

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

