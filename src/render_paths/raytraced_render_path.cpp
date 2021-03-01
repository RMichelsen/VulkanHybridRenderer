#include "pch.h"
#include "raytraced_render_path.h"

#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"
#include "render_graph/graphics_execution_context.h"
#include "render_graph/raytracing_execution_context.h"
#include "render_graph/render_graph.h"

void RaytracedRenderPath::AddPasses(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager) {
	render_graph.AddRaytracingPass("Raytracing Pass",
		{},
		{
			VkUtils::CreateTransientStorageImage("RaytracedOutput", VK_FORMAT_B8G8R8A8_UNORM, 0)
		},
		RaytracingPipelineDescription {
			.name = "Raytracing Pipeline",
			.raygen_shader = "raytraced_render_path/raygen.rgen",
			.miss_shaders = {
				"raytraced_render_path/miss.rmiss",
				"raytraced_render_path/shadow_miss.rmiss"
			},
			.hit_shaders = {
				"raytraced_render_path/closesthit.rchit"
			}
		},
		[&](ExecuteRaytracingCallback execute_pipeline) {
			execute_pipeline("Raytracing Pipeline",
				[&](RaytracingExecutionContext &execution_context) {
					execution_context.TraceRays(
						context.swapchain.extent.width,
						context.swapchain.extent.height
					);
				}
			);
		}
	);

	render_graph.AddGraphicsPass("Composition Pass",
		{
			VkUtils::CreateTransientSampledImage("RaytracedOutput", VK_FORMAT_B8G8R8A8_UNORM, 0)
		},
		{
			VkUtils::CreateTransientRenderOutput(0, ColorBlendState::Off),
		},
		{
			GraphicsPipelineDescription {
				.name = "Composition Pipeline",
				.vertex_shader = "raytraced_render_path/composition.vert",
				.fragment_shader = "raytraced_render_path/composition.frag",
				.vertex_input_state = VertexInputState::Empty,
				.rasterization_state = RasterizationState::CullCounterClockwise,
				.multisample_state = MultisampleState::Off,
				.depth_stencil_state = DepthStencilState::On,
				.dynamic_state = DynamicState::None,
				.push_constants = PUSHCONSTANTS_NONE
			}
		},
		[&](ExecuteGraphicsCallback execute_pipeline) {
			execute_pipeline("Composition Pipeline",
				[](GraphicsExecutionContext &execution_context) {
					execution_context.Draw(3, 1, 0, 0);
				}
			);
		}
	);
}

void RaytracedRenderPath::ImGuiDrawSettings() {}
