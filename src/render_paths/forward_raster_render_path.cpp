#include "pch.h"
#include "forward_raster_render_path.h"

#include "render_graph/graphics_execution_context.h"
#include "render_graph/raytracing_execution_context.h"
#include "render_graph/render_graph.h"
#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"

void ForwardRasterRenderPath::RegisterPath(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager) {
	render_graph.AddGraphicsPass("Depth Prepass",
		{},
		{
			VkUtils::CreateTransientAttachmentImage("ShadowMap", 4096, 4096, VK_FORMAT_D32_SFLOAT, 0, VkUtils::ClearDepth(0.0f))
		},
		{
			GraphicsPipelineDescription {
				.name = "Depth Prepass Pipeline",
				.vertex_shader = "forward_raster_render_path/depth_prepass.vert",
				.fragment_shader = "forward_raster_render_path/depth_prepass.frag",
				.vertex_input_state = VertexInputState::Default,
				.multisample_state = MultisampleState::Off,
				.depth_stencil_state = DepthStencilState::On,
				.dynamic_state = DynamicState::None,
				.push_constants = PushConstantDescription {
					.size = sizeof(DefaultPushConstants),
					.shader_stage = VK_SHADER_STAGE_VERTEX_BIT
				}
			}
		},
		[&](ExecuteGraphicsCallback execute_pipeline) {
			execute_pipeline("Depth Prepass Pipeline",
				[&](GraphicsExecutionContext &execution_context) {
					execution_context.BindGlobalVertexAndIndexBuffers();
					int object_id = 0;
					for(Mesh &mesh : resource_manager.scene.meshes) {
						for(int i = 0; i < mesh.primitives.size(); ++i) {
							Primitive &primitive = mesh.primitives[i];
							DefaultPushConstants push_constants {
								.object_id = object_id++
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
			VkUtils::CreateTransientRenderOutput(0, enable_msaa ? true : false),
			VkUtils::CreateTransientAttachmentImage("Depth", VK_FORMAT_D32_SFLOAT, 1, VkUtils::ClearDepth(0.0f), enable_msaa ? true : false)
		},
		{
			GraphicsPipelineDescription {
				.name = "Forward Pipeline",
				.vertex_shader = "forward_raster_render_path/default.vert",
				.fragment_shader = "forward_raster_render_path/default.frag",
				.vertex_input_state = VertexInputState::Default,
				.multisample_state = enable_msaa ? MultisampleState::On : MultisampleState::Off,
				.depth_stencil_state = DepthStencilState::On,
				.dynamic_state = DynamicState::None,
				.push_constants = PushConstantDescription {
					.size = sizeof(DefaultPushConstants),
					.shader_stage = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
				}
			}
		},
		[&](ExecuteGraphicsCallback execute_pipeline) {
			execute_pipeline("Forward Pipeline",
				[&](GraphicsExecutionContext &execution_context) {
					execution_context.BindGlobalVertexAndIndexBuffers();
					int object_id = 0;
					for(Mesh &mesh : resource_manager.scene.meshes) {
						for(int i = 0; i < mesh.primitives.size(); ++i) {
							Primitive &primitive = mesh.primitives[i];
							DefaultPushConstants push_constants {
								.object_id = object_id++
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

void ForwardRasterRenderPath::DeregisterPath(VulkanContext& context, RenderGraph& render_graph, ResourceManager& resource_manager) {}

void ForwardRasterRenderPath::ImGuiDrawSettings() {
	int old_enable_msaa = enable_msaa;

	ImGui::Text("Multisample Anti-Aliasing");
	ImGui::RadioButton("Disable", &enable_msaa, 0);
	ImGui::RadioButton("Enable", &enable_msaa, 1);
	ImGui::NewLine();

	if(old_enable_msaa != enable_msaa) {
		Rebuild();
	}
}
