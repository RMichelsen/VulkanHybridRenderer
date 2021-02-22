#include "pch.h"
#include "hybrid_shadows_render_path.h"

#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"
#include "render_graph/graphics_execution_context.h"
#include "render_graph/raytracing_execution_context.h"
#include "render_graph/render_graph.h"

namespace HybridShadowsRenderPath {
void Enable(VulkanContext &context, ResourceManager &resource_manager, RenderGraph &render_graph) {
	render_graph.AddGraphicsPass("G-Buffer Pass",
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
				.vertex_shader = "hybrid_shadows/gbuf.vert",
				.fragment_shader = "hybrid_shadows/gbuf.frag",
				.vertex_input_state = VertexInputState::Default,
				.rasterization_state = RasterizationState::CullCounterClockwise,
				.multisample_state = MultisampleState::Off,
				.depth_stencil_state = DepthStencilState::On,
				.dynamic_state = DynamicState::None,
				.push_constants = PushConstantDescription {
					.size = sizeof(PushConstants),
					.pipeline_stage = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
				}
			}
		},
		[&](ExecuteGraphicsCallback execute_pipeline) {
			execute_pipeline("G-Buffer Pipeline",
				[&](GraphicsExecutionContext &execution_context) {
					execution_context.BindGlobalVertexAndIndexBuffers();
					for(Mesh &mesh : resource_manager.scene.meshes) {
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

	render_graph.AddRaytracingPass("Raytracing Pass",
		{},
		{
			VkUtils::CreateTransientSampledImage("Position", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
			VkUtils::CreateTransientStorageImage("RaytracedShadows", VK_FORMAT_B8G8R8A8_UNORM, 1)
		},
		RaytracingPipelineDescription {
			.name = "Raytracing Pipeline",
			.raygen_shader = "hybrid_shadows/raygen.rgen",
			.miss_shaders = {
				"hybrid_shadows/miss.rmiss",
			},
			.hit_shaders = {}
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
			VkUtils::CreateTransientSampledImage("Position", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
			VkUtils::CreateTransientSampledImage("Normal", VK_FORMAT_R16G16B16A16_SFLOAT, 1),
			VkUtils::CreateTransientSampledImage("Albedo", VK_FORMAT_B8G8R8A8_UNORM, 2),
			VkUtils::CreateTransientSampledImage("RaytracedShadows", VK_FORMAT_B8G8R8A8_UNORM, 3)
		},
		{
			VkUtils::CreateTransientRenderOutput(0, ColorBlendState::Off),
			VkUtils::CreateTransientAttachmentImage("Depth", VK_FORMAT_D32_SFLOAT, 1)
		},
		{
			GraphicsPipelineDescription {
				.name = "Composition Pipeline",
				.vertex_shader = "hybrid_shadows/composition.vert",
				.fragment_shader = "hybrid_shadows/composition.frag",
				.vertex_input_state = VertexInputState::Empty,
				.rasterization_state = RasterizationState::CullClockwise,
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
}