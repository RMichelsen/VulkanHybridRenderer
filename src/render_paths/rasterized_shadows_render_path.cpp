#include "pch.h"
#include "hybrid_shadows_render_path.h"

#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"
#include "render_graph/graphics_execution_context.h"
#include "render_graph/raytracing_execution_context.h"
#include "render_graph/render_graph.h"

namespace RasterizedShadowsRenderPath {
struct RasterizedShadowsPushConstants {
	glm::mat4 light_projview;
	int object_id;
};

void Enable(VulkanContext &context, ResourceManager &resource_manager, RenderGraph &render_graph) {
	render_graph.AddGraphicsPass("Depth Prepass",
		{},
		{
			VkUtils::CreateTransientAttachmentImage("ShadowMap", 4096, 4096, VK_FORMAT_D32_SFLOAT, 0)
		},
		{
			GraphicsPipelineDescription {
				.name = "Depth Pipeline",
				.vertex_shader = "rasterized_shadows/depth.vert",
				.fragment_shader = "rasterized_shadows/depth.frag",
				.vertex_input_state = VertexInputState::Default,
				.rasterization_state = RasterizationState::CullCounterClockwise,
				.multisample_state = MultisampleState::Off,
				.depth_stencil_state = DepthStencilState::On,
				.dynamic_state = DynamicState::None,
				.push_constants = PushConstantDescription {
					.size = sizeof(RasterizedShadowsPushConstants),
					.pipeline_stage = VK_SHADER_STAGE_VERTEX_BIT
				}
			}
		},
		[&](ExecuteGraphicsCallback execute_pipeline) {
			execute_pipeline("Depth Pipeline",
				[&](GraphicsExecutionContext &execution_context) {
					glm::mat4 light_perspective = glm::ortho(-30.0f, 30.0f, -30.0f, 30.0f, 1.0f, 64.0f);
					light_perspective[1][1] *= -1.0f;
					glm::mat4 light_view = glm::lookAt(
						glm::vec3(0.0f, -60.0f, 0.1f),
						glm::vec3(0.0f, 0.0f, 0.0f),
						glm::vec3(0.0f, -1.0f, 0.0f)
					);
					glm::mat4 light_projview = light_perspective * light_view;

					execution_context.BindGlobalVertexAndIndexBuffers();
					for(Mesh &mesh : resource_manager.scene.meshes) {
						for(int i = 0; i < mesh.primitives.size(); ++i) {
							Primitive &primitive = mesh.primitives[i];
							RasterizedShadowsPushConstants push_constants {
								.light_projview = light_projview,
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

	render_graph.AddGraphicsPass("Forward Pass",
		{
			VkUtils::CreateTransientSampledImage("ShadowMap", 4096, 4096, VK_FORMAT_D32_SFLOAT, 0)
		},
		{
			VkUtils::CreateTransientRenderOutput(0, ColorBlendState::Off),
			VkUtils::CreateTransientAttachmentImage("Depth", VK_FORMAT_D32_SFLOAT, 1)
		},
		{
			GraphicsPipelineDescription {
				.name = "Forward Pipeline",
				.vertex_shader = "rasterized_shadows/default.vert",
				.fragment_shader = "rasterized_shadows/default.frag",
				.vertex_input_state = VertexInputState::Default,
				.rasterization_state = RasterizationState::CullCounterClockwise,
				.multisample_state = MultisampleState::Off,
				.depth_stencil_state = DepthStencilState::On,
				.dynamic_state = DynamicState::None,
				.push_constants = PushConstantDescription {
					.size = sizeof(RasterizedShadowsPushConstants),
					.pipeline_stage = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
				}
			}
		},
		[&](ExecuteGraphicsCallback execute_pipeline) {
			execute_pipeline("Forward Pipeline",
				[&](GraphicsExecutionContext &execution_context) {
					glm::mat4 light_perspective = glm::ortho(-30.0f, 30.0f, -30.0f, 30.0f, 1.0f, 64.0f);
					light_perspective[1][1] *= -1.0f;
					glm::mat4 light_view = glm::lookAt(
						glm::vec3(0.0f, -60.0f, 0.1f),
						glm::vec3(0.0f, 0.0f, 0.0f),
						glm::vec3(0.0f, -1.0f, 0.0f)
					);
					glm::mat4 light_projview = light_perspective * light_view;

					execution_context.BindGlobalVertexAndIndexBuffers();
					//execution_context.SetDepthBias(1.25f, 0.0f, 1.75f);
					for(Mesh &mesh : resource_manager.scene.meshes) {
						for(int i = 0; i < mesh.primitives.size(); ++i) {
							Primitive &primitive = mesh.primitives[i];
							RasterizedShadowsPushConstants push_constants {
								.light_projview = light_projview,
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