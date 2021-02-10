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

Renderer::Renderer(HINSTANCE hinstance, HWND hwnd) {
	context = std::make_unique<VulkanContext>(hinstance, hwnd);
	resource_manager = std::make_unique<ResourceManager>(*context);
	render_graph = std::make_unique<RenderGraph>(*context, *resource_manager);
	user_interface = std::make_unique<UserInterface>(*context, *resource_manager);
	scene = SceneLoader::LoadScene(*resource_manager, "data/models/Sponza.glb");

	CreatePipeline();
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

void Renderer::Render(FrameResources &resources, uint32_t resource_idx, uint32_t image_idx) {
	resource_manager->UpdatePerFrameUBO(resource_idx, 
		PerFrameData {
			.camera_view = scene.camera.view,
			.camera_proj = scene.camera.perspective,
			.camera_view_inverse = glm::inverse(scene.camera.view),
			.camera_proj_inverse = glm::inverse(scene.camera.perspective)
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

void Renderer::CreatePipeline() {

	render_graph->AddGraphicsPass("G-Buffer Pass",
		{},
		{
			CreateTransientAttachmentImage("Position", VK_FORMAT_R16G16B16A16_SFLOAT),
			CreateTransientAttachmentImage("Normal", VK_FORMAT_R16G16B16A16_SFLOAT),
			CreateTransientAttachmentImage("Albedo", VK_FORMAT_B8G8R8A8_UNORM),
			CreateTransientAttachmentImage("Depth", VK_FORMAT_D32_SFLOAT)
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
							PushConstants push_constants {
								.object_id = i
							};
							execution_context.PushConstants(push_constants);
							execution_context.DrawIndexed(mesh.primitives[i].index_count, 1,
								mesh.primitives[i].index_offset, mesh.primitives[i].vertex_offset, 0);
						}
					}
				}
			);
		}
	);

	render_graph->AddRaytracingPass("Raytracing Pass",
		{},
		{
			CreateTransientStorageImage("Rays", VK_FORMAT_B8G8R8A8_UNORM, 0)
		},
		RaytracingPipelineDescription {
			.name = "Raytracing Pipeline",
			.raygen_shader = "data/shaders/compiled/raygen.rgen.spv",
			.hit_shader = "data/shaders/compiled/closesthit.rchit.spv",
			.miss_shader = "data/shaders/compiled/miss.rmiss.spv"
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
			CreateTransientSampledImage("Position", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
			CreateTransientSampledImage("Normal", VK_FORMAT_R16G16B16A16_SFLOAT, 1),
			CreateTransientSampledImage("Albedo", VK_FORMAT_B8G8R8A8_UNORM, 2),
			CreateTransientSampledImage("Rays", VK_FORMAT_B8G8R8A8_UNORM, 3)
		},
		{
			CreateTransientBackbuffer(ColorBlendState::ImGui),
			CreateTransientAttachmentImage("Depth", VK_FORMAT_D32_SFLOAT)
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
				[this](GraphicsExecutionContext &execution_context) {
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
}

TransientResource Renderer::CreateTransientBackbuffer(ColorBlendState color_blend_state) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = "BACKBUFFER",
		.image = TransientImage {
			.type = TransientImageType::AttachmentImage,
			.width = context->swapchain.extent.width,
			.height = context->swapchain.extent.height,
			.format = VK_FORMAT_UNDEFINED,
			.attachment_image = TransientAttachmentImage {
				.color_blend_state = color_blend_state
			}
		}
	};
}

TransientResource Renderer::CreateTransientAttachmentImage(const char *name, VkFormat format,
	ColorBlendState color_blend_state) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = name,
		.image = TransientImage {
			.type = TransientImageType::AttachmentImage,
			.width = 0,
			.height = 0,
			.format = format,
			.attachment_image = TransientAttachmentImage {
				.color_blend_state = color_blend_state
			}
		}
	};
}

TransientResource Renderer::CreateTransientAttachmentImage(const char *name, uint32_t width, uint32_t height, 
	VkFormat format, ColorBlendState color_blend_state) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = name,
		.image = TransientImage {
			.type = TransientImageType::AttachmentImage,
			.width = width,
			.height = height,
			.format = format,
			.attachment_image = TransientAttachmentImage {
				.color_blend_state = color_blend_state
			}
		}
	};
}

TransientResource Renderer::CreateTransientSampledImage(const char *name, VkFormat format,
	uint32_t binding) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = name,
		.image = TransientImage {
			.type = TransientImageType::SampledImage,
			.width = 0,
			.height = 0,
			.format = format,
			.sampled_image = TransientSampledImage {
				.binding = binding,
				.sampler = resource_manager->sampler
			}
		}
	};
}

TransientResource Renderer::CreateTransientSampledImage(const char *name, uint32_t width,
	uint32_t height, VkFormat format, uint32_t binding) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = name,
		.image = TransientImage {
			.type = TransientImageType::SampledImage,
			.width = width,
			.height = height,
			.format = format,
			.sampled_image = TransientSampledImage {
				.binding = binding,
				.sampler = resource_manager->sampler
			}
		}
	};
}

TransientResource Renderer::CreateTransientStorageImage(const char *name, VkFormat format, 
	uint32_t binding) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = name,
		.image = TransientImage {
			.type = TransientImageType::StorageImage,
			.width = 0,
			.height = 0,
			.format = format,
			.storage_image = TransientStorageImage {
				.binding = binding
			}
		}
	};
}

TransientResource Renderer::CreateTransientStorageImage(const char *name, uint32_t width,
	uint32_t height, VkFormat format, uint32_t binding) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = name,
		.image = TransientImage {
			.type = TransientImageType::StorageImage,
			.width = width,
			.height = height,
			.format = format,
			.storage_image = TransientStorageImage {
				.binding = binding
			}
		}
	};
}

