#include "pch.h"
#include "hybrid_render_path.h"

#include "render_graph/graphics_execution_context.h"
#include "render_graph/raytracing_execution_context.h"
#include "render_graph/render_graph.h"
#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"

void HybridRenderPath::AddPasses(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager) {
	if(shadow_mode == SHADOW_MODE_RASTERIZED) {
		render_graph.AddGraphicsPass("Depth Prepass",
			{},
			{
				VkUtils::CreateTransientAttachmentImage("ShadowMap", 4096, 4096, VK_FORMAT_D32_SFLOAT, 0)
			},
			{
				GraphicsPipelineDescription {
					.name = "Depth Prepass Pipeline",
					.vertex_shader = "hybrid_render_path/depth_prepass.vert",
					.fragment_shader = "hybrid_render_path/depth_prepass.frag",
					.vertex_input_state = VertexInputState::Default,
					.multisample_state = MultisampleState::Off,
					.depth_stencil_state = DepthStencilState::On,
					.dynamic_state = DynamicState::None,
					.push_constants = PushConstantDescription {
						.size = sizeof(PushConstants),
						.pipeline_stage = VK_SHADER_STAGE_VERTEX_BIT
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
								PushConstants push_constants {
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
	else if(shadow_mode == SHADOW_MODE_RAYTRACED) {
		render_graph.AddRaytracingPass("Raytraced Shadows Pass",
			{},
			{
				VkUtils::CreateTransientSampledImage("Position", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
				VkUtils::CreateTransientStorageImage("RaytracedShadows", VK_FORMAT_B8G8R8A8_UNORM, 1)
			},
			RaytracingPipelineDescription {
				.name = "Raytraced Shadows Pipeline",
				.raygen_shader = "hybrid_render_path/shadows.rgen",
				.miss_shaders = {
					"hybrid_render_path/shadows.rmiss"
				},
				.hit_shaders = { 
					HitShader {
						.any_hit = "hybrid_render_path/shadows.rahit"
					}
				}
			},
			[&](ExecuteRaytracingCallback execute_pipeline) {
				execute_pipeline("Raytraced Shadows Pipeline",
					[&](RaytracingExecutionContext &execution_context) {
						execution_context.TraceRays(
							context.swapchain.extent.width,
							context.swapchain.extent.height
						);
					}
				);
			}
		);
	}

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
				.vertex_shader = "hybrid_render_path/gbuf.vert",
				.fragment_shader = "hybrid_render_path/gbuf.frag",
				.vertex_input_state = VertexInputState::Default,
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
					int object_id = 0;
					for(Mesh &mesh : resource_manager.scene.meshes) {
						for(int i = 0; i < mesh.primitives.size(); ++i) {
							Primitive &primitive = mesh.primitives[i];
							PushConstants push_constants {
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


	render_graph.AddGraphicsPass("Composition Pass",
		{
			VkUtils::CreateTransientSampledImage("Position", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
			VkUtils::CreateTransientSampledImage("Normal", VK_FORMAT_R16G16B16A16_SFLOAT, 1),
			VkUtils::CreateTransientSampledImage("Albedo", VK_FORMAT_B8G8R8A8_UNORM, 2),
			VkUtils::CreateTransientSampledImage("ShadowMap", 4096, 4096, VK_FORMAT_D32_SFLOAT, 3),
			VkUtils::CreateTransientSampledImage("RaytracedShadows", VK_FORMAT_B8G8R8A8_UNORM, 4)
		},
		{
			VkUtils::CreateTransientRenderOutput(0, ColorBlendState::Off),
			VkUtils::CreateTransientAttachmentImage("Depth", VK_FORMAT_D32_SFLOAT, 1)
		},
		{
			GraphicsPipelineDescription {
				.name = "Composition Pipeline",
				.vertex_shader = "hybrid_render_path/composition.vert",
				.fragment_shader = "hybrid_render_path/composition.frag",
				.vertex_input_state = VertexInputState::Empty,
				.multisample_state = MultisampleState::Off,
				.depth_stencil_state = DepthStencilState::On,
				.dynamic_state = DynamicState::None,
				.push_constants = PUSHCONSTANTS_NONE,
				.specialization_constants_description = SpecializationConstantsDescription {
					.shader_stage = VK_SHADER_STAGE_FRAGMENT_BIT,
					.specialization_constants = { shadow_mode }
				}
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

void HybridRenderPath::ImGuiDrawSettings() {
	int old_shadow_mode = shadow_mode;

	ImGui::Text("Shadow Mode:");
	ImGui::RadioButton("Raytraced Shadows", &shadow_mode, SHADOW_MODE_RAYTRACED);
	ImGui::RadioButton("Rasterized Shadows", &shadow_mode, SHADOW_MODE_RASTERIZED);
	ImGui::RadioButton("No Shadows", &shadow_mode, SHADOW_MODE_OFF);
	ImGui::NewLine();

	if(old_shadow_mode != shadow_mode) {
		Build();
	}
}
