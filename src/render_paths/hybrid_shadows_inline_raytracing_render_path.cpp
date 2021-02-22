#include "pch.h"
#include "hybrid_shadows_inline_raytracing_render_path.h"

#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"
#include "render_graph/graphics_execution_context.h"
#include "render_graph/raytracing_execution_context.h"
#include "render_graph/render_graph.h"

namespace HybridShadowsInlineRaytracingRenderPath {
void Enable(VulkanContext &context, ResourceManager &resource_manager, RenderGraph &render_graph) {
	render_graph.AddGraphicsPass("Forward Pass",
		{
		},
		{
			VkUtils::CreateTransientRenderOutput(0, ColorBlendState::Off),
			VkUtils::CreateTransientAttachmentImage("Depth", VK_FORMAT_D32_SFLOAT, 1)
		},
		{
			GraphicsPipelineDescription {
				.name = "Forward Pipeline",
				.vertex_shader = "hybrid_shadows_inline_raytracing/default.vert",
				.fragment_shader = "hybrid_shadows_inline_raytracing/default.frag",
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
		[&](ExecuteGraphicsCallback execute_pipeline) {
			execute_pipeline("Forward Pipeline",
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
}
}