#include "pch.h"
#include "hybrid_render_path.h"

#include "render_graph/compute_execution_context.h"
#include "render_graph/graphics_execution_context.h"
#include "render_graph/raytracing_execution_context.h"
#include "render_graph/render_graph.h"
#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"

void HybridRenderPath::RegisterPath(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager) {
	if(shadow_mode == SHADOW_MODE_RASTERIZED) {
		render_graph.AddGraphicsPass("Depth Prepass",
			{},
			{
				VkUtils::CreateTransientAttachmentImage("Shadow Map", 4096, 4096, VK_FORMAT_D32_SFLOAT, 0)
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
						.size = sizeof(HybridPushConstants),
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
								HybridPushConstants push_constants {
									.normal_matrix = glm::mat3(glm::transpose(glm::inverse(primitive.transform))),
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
	else if(shadow_mode == SHADOW_MODE_RAYTRACED || ambient_occlusion_mode == AMBIENT_OCCLUSION_MODE_RAYTRACED) {
		render_graph.AddRaytracingPass("Raytrace Pass",
			{
				VkUtils::CreateTransientSampledImage("World Space Position", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
				VkUtils::CreateTransientSampledImage("World Space Normals", VK_FORMAT_R16G16B16A16_SFLOAT, 1)
			},
			{
				VkUtils::CreateTransientStorageImage("Raytraced Shadows", VK_FORMAT_R16G16B16A16_SFLOAT, 2),
				VkUtils::CreateTransientStorageImage("Raytraced Ambient Occlusion", VK_FORMAT_R16G16B16A16_SFLOAT, 3)
			},
			RaytracingPipelineDescription {
				.name = "Raytrace Pipeline",
				.raygen_shader = "hybrid_render_path/raygen.rgen",
				.miss_shaders = {
					"hybrid_render_path/miss.rmiss"
				},
				.hit_shaders = {}
			},
			[&](ExecuteRaytracingCallback execute_pipeline) {
				execute_pipeline("Raytrace Pipeline",
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
			VkUtils::CreateTransientAttachmentImage("World Space Position", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
			VkUtils::CreateTransientAttachmentImage("World Space Normals", VK_FORMAT_R16G16B16A16_SFLOAT, 1),
			VkUtils::CreateTransientAttachmentImage("Albedo", VK_FORMAT_B8G8R8A8_UNORM, 2),
			VkUtils::CreateTransientAttachmentImage("Reprojected UV and Depth Derivatives", VK_FORMAT_R16G16B16A16_SFLOAT, 3,
				VkClearValue {
					.color = VkClearColorValue {
						.float32 = { 0.0f, 0.0f, -1.0f, 1.0f }
					}
			}),
			VkUtils::CreateTransientAttachmentImage("Depth", VK_FORMAT_D32_SFLOAT, 4),
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
					.size = sizeof(HybridPushConstants),
					.shader_stage = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
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
							HybridPushConstants push_constants {
								.normal_matrix = glm::inverseTranspose(glm::mat3(primitive.transform)),
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

	// TODO: Fix leak, cleanup when rebuilding paths
	svgf_push_constants.prev_frame_normals_and_object_id =
		resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
			context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);
	svgf_push_constants.shadow_history =
		resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
			context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);
	svgf_push_constants.shadow_moments_history =
		resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
			context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);
	svgf_push_constants.ambient_occlusion_history =
		resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
			context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);
	svgf_push_constants.ambient_occlusion_moments_history =
		resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
			context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);
	svgf_push_constants.integrated_shadows.x =
		resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
			context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);
	svgf_push_constants.integrated_shadows.y =
		resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
			context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);
	svgf_push_constants.integrated_ambient_occlusion.x =
		resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
			context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);
	svgf_push_constants.integrated_ambient_occlusion.y =
		resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
			context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);

	render_graph.AddComputePass("SVGF Denoise Pass",
		{
			VkUtils::CreateTransientStorageImage("Reprojected UV and Depth Derivatives", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
			VkUtils::CreateTransientStorageImage("World Space Position", VK_FORMAT_R16G16B16A16_SFLOAT, 1),
			VkUtils::CreateTransientStorageImage("World Space Normals", VK_FORMAT_R16G16B16A16_SFLOAT, 2),
			VkUtils::CreateTransientStorageImage("Raytraced Shadows", VK_FORMAT_R16G16B16A16_SFLOAT, 3),
			VkUtils::CreateTransientStorageImage("Raytraced Ambient Occlusion", VK_FORMAT_R16G16B16A16_SFLOAT, 4),
		},
		{
			VkUtils::CreateTransientStorageImage("Denoised Raytraced Shadows", VK_FORMAT_R16G16B16A16_SFLOAT, 5),
			VkUtils::CreateTransientStorageImage("Denoised Raytraced Ambient Occlusion", VK_FORMAT_R16G16B16A16_SFLOAT, 6),
		},
		ComputePipelineDescription {
			.kernels = {
				ComputeKernel {
					.shader = "hybrid_render_path/svgf.comp"
				},
				ComputeKernel {
					.shader = "hybrid_render_path/svgf_final_copy.comp"
				},
				ComputeKernel {
					.shader = "hybrid_render_path/svgf_atrous_filter.comp"
				},
				ComputeKernel {
					.shader = "hybrid_render_path/svgf_copy_filtered_shadow.comp"
				}
			},
			.push_constant_description = PushConstantDescription {
				.size = sizeof(SVGFPushConstants),
				.shader_stage = VK_SHADER_STAGE_COMPUTE_BIT
			}
		},
		[&](ComputeExecutionContext &execution_context) {
			glm::uvec2 display_size = execution_context.GetDisplaySize();

			// TODO: Fix push constants requiring a name (its unnecessary probably)
			execution_context.PushConstants("hybrid_render_path/svgf.comp", svgf_push_constants);
			execution_context.Dispatch("hybrid_render_path/svgf.comp",
				display_size.x / 8 + (display_size.x % 8 != 0),
				display_size.y / 8 + (display_size.y % 8 != 0),
				1
			);

			int atrous_steps = 5;
			for(int i = 1; i <= atrous_steps; ++i) {
				svgf_push_constants.atrous_step = 1 << i;
				execution_context.PushConstants("hybrid_render_path/svgf_atrous_filter.comp", svgf_push_constants);
				execution_context.Dispatch("hybrid_render_path/svgf_atrous_filter.comp",
					display_size.x / 8 + (display_size.x % 8 != 0),
					display_size.y / 8 + (display_size.y % 8 != 0),
					1
				);

				// Copy integrated shadows
				if(i == 1) {
					execution_context.Dispatch("hybrid_render_path/svgf_copy_filtered_shadow.comp",
						display_size.x / 8 + (display_size.x % 8 != 0),
						display_size.y / 8 + (display_size.y % 8 != 0),
						1
					);
				}

				// Swap pingpong
				std::swap(svgf_push_constants.integrated_shadows.x, svgf_push_constants.integrated_shadows.y);
				std::swap(svgf_push_constants.integrated_ambient_occlusion.x, svgf_push_constants.integrated_ambient_occlusion.y);
			}

			execution_context.Dispatch("hybrid_render_path/svgf_final_copy.comp",
				display_size.x / 8 + (display_size.x % 8 != 0),
				display_size.y / 8 + (display_size.y % 8 != 0),
				1
			);

			// Swap to prepare for next frame
			std::swap(svgf_push_constants.integrated_shadows.x, svgf_push_constants.integrated_shadows.y);
			std::swap(svgf_push_constants.integrated_ambient_occlusion.x, svgf_push_constants.integrated_ambient_occlusion.y);
		}
	);

	render_graph.AddGraphicsPass("Composition Pass",
		{
			VkUtils::CreateTransientSampledImage("World Space Position", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
			VkUtils::CreateTransientSampledImage("World Space Normals", VK_FORMAT_R16G16B16A16_SFLOAT, 1),
			VkUtils::CreateTransientSampledImage("Albedo", VK_FORMAT_B8G8R8A8_UNORM, 2),
			VkUtils::CreateTransientSampledImage("Shadow Map", 4096, 4096, VK_FORMAT_D32_SFLOAT, 3),
			denoise_shadows ?
				VkUtils::CreateTransientSampledImage("Denoised Raytraced Shadows", VK_FORMAT_R16G16B16A16_SFLOAT, 4) :
				VkUtils::CreateTransientSampledImage("Raytraced Shadows", VK_FORMAT_R16G16B16A16_SFLOAT, 4),
			denoise_ambient_occlusion ?
				VkUtils::CreateTransientSampledImage("Denoised Raytraced Ambient Occlusion", VK_FORMAT_R16G16B16A16_SFLOAT, 5) :
				VkUtils::CreateTransientSampledImage("Raytraced Ambient Occlusion", VK_FORMAT_R16G16B16A16_SFLOAT, 5)
		},
		{
			VkUtils::CreateTransientRenderOutput(0),
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
					.specialization_constants = { shadow_mode, ambient_occlusion_mode }
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
	int old_ambient_occlusion_mode = ambient_occlusion_mode;
	bool old_denoise_shadows = denoise_shadows;
	bool old_denoise_ambient_occlusion = denoise_ambient_occlusion;

	ImGui::Text("Shadow Mode:");
	ImGui::RadioButton("Raytraced Shadows", &shadow_mode, SHADOW_MODE_RAYTRACED);
	ImGui::SameLine(); 	ImGui::Checkbox("Denoise Shadows", &denoise_shadows);
	ImGui::RadioButton("Rasterized Shadows", &shadow_mode, SHADOW_MODE_RASTERIZED);
	ImGui::RadioButton("No Shadows", &shadow_mode, SHADOW_MODE_OFF);
	ImGui::NewLine();
	ImGui::NewLine();

	ImGui::Text("Ambient Occlusion Mode:");
	ImGui::RadioButton("Raytraced Ambient Occlusion", &ambient_occlusion_mode, SHADOW_MODE_RAYTRACED);
	ImGui::SameLine(); 	ImGui::Checkbox("Denoise Ambient Occlusion", &denoise_ambient_occlusion);
	ImGui::RadioButton("Screen-Space Ambient Occlusion", &ambient_occlusion_mode, SHADOW_MODE_RASTERIZED);
	ImGui::RadioButton("No Ambient Occlusion", &ambient_occlusion_mode, SHADOW_MODE_OFF);
	ImGui::NewLine();

	if(old_shadow_mode != shadow_mode || 
	   old_ambient_occlusion_mode != ambient_occlusion_mode ||
	   old_denoise_shadows != denoise_shadows ||
	   old_denoise_ambient_occlusion != denoise_ambient_occlusion) {
		Build();
	}
}
