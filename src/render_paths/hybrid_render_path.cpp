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
	render_graph.AddGraphicsPass("G-Buffer Pass",
		{},
		{
			VkUtils::CreateTransientAttachmentImage("Albedo", VK_FORMAT_B8G8R8A8_UNORM, 0, VkUtils::ClearColor(0.0f, 0.0f, 0.0f, 0.0f)),
			VkUtils::CreateTransientAttachmentImage("World Space Normals and Linear Depths", VK_FORMAT_R16G16B16A16_SFLOAT, 1, VkUtils::ClearColor(0.0f, 0.0f, 0.0f, 0.0f)),
			VkUtils::CreateTransientAttachmentImage("Motion Vectors and Metallic Roughness", VK_FORMAT_R16G16B16A16_SFLOAT, 2, VkUtils::ClearColor(0.0f, 0.0f, -1.0f, -1.0f)),
			VkUtils::CreateTransientAttachmentImage("Depth", VK_FORMAT_D32_SFLOAT, 3, VkUtils::ClearDepth(0.0f))
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
	
	if(shadow_mode == SHADOW_MODE_RASTERIZED) {
		render_graph.AddGraphicsPass("Depth Prepass",
			{},
			{
				VkUtils::CreateTransientAttachmentImage("Shadow Map", 4096, 4096, VK_FORMAT_D32_SFLOAT, 0, VkUtils::ClearDepth(0.0f))
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
	}
	else if(shadow_mode == SHADOW_MODE_RAYTRACED || ambient_occlusion_mode == AMBIENT_OCCLUSION_MODE_RAYTRACED ||
		    reflection_mode == REFLECTION_MODE_RAYTRACED) {
		render_graph.AddRaytracingPass("Raytrace Pass",
			{
				VkUtils::CreateTransientSampledImage("World Space Normals and Linear Depths", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
				VkUtils::CreateTransientSampledImage("Depth", VK_FORMAT_D32_SFLOAT, 1),
			},
			{
				VkUtils::CreateTransientStorageImage("Raytraced Shadows and Ambient Occlusion", VK_FORMAT_R16G16B16A16_SFLOAT, 2),
				VkUtils::CreateTransientStorageImage("Raytraced Reflections", VK_FORMAT_R16G16B16A16_SFLOAT, 3)
			},
			RaytracingPipelineDescription {
				.name = "Raytrace Pipeline",
				.raygen_shader = "hybrid_render_path/raygen.rgen",
				.miss_shaders = {
					"hybrid_render_path/miss.rmiss",
					"hybrid_render_path/reflection_miss.rmiss"
				},
				.hit_shaders = {
					HitShader {
						.closest_hit = "hybrid_render_path/reflection_hit.rchit"
					}
				}
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

	if(ambient_occlusion_mode == AMBIENT_OCCLUSION_MODE_SSAO) {
		render_graph.AddComputePass("SSAO Pass",
			{
				VkUtils::CreateTransientSampledImage("World Space Normals and Linear Depths", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
				VkUtils::CreateTransientSampledImage("Depth", VK_FORMAT_D32_SFLOAT, 1),
			},
			{
				VkUtils::CreateTransientStorageImage("Screen Space Ambient Occlusion Raw", VK_FORMAT_R16G16B16A16_SFLOAT, 2)
			},
			ComputePipelineDescription {
				.kernels = {
					ComputeKernel {
						.shader = "hybrid_render_path/ssao.comp"
					}
				}
			},
			[&](ComputeExecutionContext &execution_context) {
				glm::uvec2 display_size = execution_context.GetDisplaySize();

				execution_context.Dispatch(
					"hybrid_render_path/ssao.comp",
					display_size.x / 8 + (display_size.x % 8 != 0),
					display_size.y / 8 + (display_size.y % 8 != 0),
					1
				);
			}
		);

		render_graph.AddComputePass("SSAO Blur Pass",
			{
				VkUtils::CreateTransientStorageImage("Screen Space Ambient Occlusion Raw", VK_FORMAT_R16G16B16A16_SFLOAT, 0)
			},
			{
				VkUtils::CreateTransientStorageImage("Screen Space Ambient Occlusion", VK_FORMAT_R16G16B16A16_SFLOAT, 1)
			},
			ComputePipelineDescription {
				.kernels = {
					ComputeKernel {
						.shader = "hybrid_render_path/ssao_blur.comp"
					}
				}
			},
			[&](ComputeExecutionContext &execution_context) {
				glm::uvec2 display_size = execution_context.GetDisplaySize();

				execution_context.Dispatch(
					"hybrid_render_path/ssao_blur.comp",
					display_size.x / 8 + (display_size.x % 8 != 0),
					display_size.y / 8 + (display_size.y % 8 != 0),
					1
				);
			}
		);
	}

	if(reflection_mode == REFLECTION_MODE_SSR) {
		render_graph.AddComputePass("SSR Pass",
			{
				VkUtils::CreateTransientSampledImage("Albedo", VK_FORMAT_B8G8R8A8_UNORM, 0),
				VkUtils::CreateTransientSampledImage("World Space Normals and Linear Depths", VK_FORMAT_R16G16B16A16_SFLOAT, 1),
				VkUtils::CreateTransientSampledImage("Motion Vectors and Metallic Roughness", VK_FORMAT_R16G16B16A16_SFLOAT, 2),
				VkUtils::CreateTransientSampledImage("Depth", VK_FORMAT_D32_SFLOAT, 3),
			},
			{
				VkUtils::CreateTransientStorageImage("Screen Space Reflections", VK_FORMAT_R16G16B16A16_SFLOAT, 4)
			},
			ComputePipelineDescription {
				.kernels = {
					ComputeKernel {
						.shader = "hybrid_render_path/ssr.comp"
					}
				}
			},
			[&](ComputeExecutionContext &execution_context) {
				glm::uvec2 display_size = execution_context.GetDisplaySize();

				execution_context.Dispatch(
					"hybrid_render_path/ssr.comp",
					display_size.x / 8 + (display_size.x % 8 != 0),
					display_size.y / 8 + (display_size.y % 8 != 0),
					1
				);
			}
		);
	}

	if(denoise_shadow_and_ao && (shadow_mode == SHADOW_MODE_RAYTRACED || ambient_occlusion_mode == AMBIENT_OCCLUSION_MODE_RAYTRACED ||
		reflection_mode == REFLECTION_MODE_RAYTRACED)) {
		svgf_push_constants.integrated_shadow_and_ao.x =
			resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
				context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);
		svgf_push_constants.integrated_shadow_and_ao.y =
			resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
				context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);
		svgf_push_constants.prev_frame_normals_and_linear_depths =
			resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
				context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);
		svgf_push_constants.shadow_and_ao_history =
			resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
				context.swapchain.extent.height, VK_FORMAT_R16G16B16A16_SFLOAT);
		svgf_push_constants.shadow_and_ao_moments_history =
			resource_manager.UploadNewStorageImage(context.swapchain.extent.width,
				context.swapchain.extent.height, VK_FORMAT_R16G16_SFLOAT);
		svgf_textures_created = true;

		render_graph.AddComputePass("SVGF Denoise Pass",
			{
				VkUtils::CreateTransientStorageImage("World Space Normals and Linear Depths", VK_FORMAT_R16G16B16A16_SFLOAT, 0),
				VkUtils::CreateTransientStorageImage("Motion Vectors and Metallic Roughness", VK_FORMAT_R16G16B16A16_SFLOAT, 1),
				VkUtils::CreateTransientSampledImage("Depth", VK_FORMAT_D32_SFLOAT, 2),
				VkUtils::CreateTransientStorageImage("Raytraced Shadows and Ambient Occlusion", VK_FORMAT_R16G16B16A16_SFLOAT, 3),
			},
			{
				VkUtils::CreateTransientStorageImage("Denoised Raytraced Shadows and Ambient Occlusion", VK_FORMAT_R16G16B16A16_SFLOAT, 4),
			},
			ComputePipelineDescription {
				.kernels = {
					ComputeKernel {
						.shader = "hybrid_render_path/svgf.comp"
					},
					ComputeKernel {
						.shader = "hybrid_render_path/svgf_atrous_filter.comp"
					},
				},
				.push_constant_description = PushConstantDescription {
					.size = sizeof(SVGFPushConstants),
					.shader_stage = VK_SHADER_STAGE_COMPUTE_BIT
				}
			},
			[&](ComputeExecutionContext &execution_context) {
				glm::uvec2 display_size = execution_context.GetDisplaySize();

				execution_context.Dispatch(
					"hybrid_render_path/svgf.comp",
					display_size.x / 8 + (display_size.x % 8 != 0),
					display_size.y / 8 + (display_size.y % 8 != 0),
					1,
					svgf_push_constants
				);

				int atrous_steps = 5;
				for(int i = 0; i < atrous_steps; ++i) {
					svgf_push_constants.atrous_step = 1 << i;
					execution_context.Dispatch("hybrid_render_path/svgf_atrous_filter.comp",
						display_size.x / 8 + (display_size.x % 8 != 0),
						display_size.y / 8 + (display_size.y % 8 != 0),
						1,
						svgf_push_constants
					);

					// Copy integrated shadows
					if(i == 0) {
						execution_context.BlitImageStorageToStorage(
							svgf_push_constants.integrated_shadow_and_ao.y,
							svgf_push_constants.shadow_and_ao_history
						);
					}

					// Swap pingpong
					std::swap(svgf_push_constants.integrated_shadow_and_ao.x, svgf_push_constants.integrated_shadow_and_ao.y);
				}

				execution_context.BlitImageTransientToStorage("World Space Normals and Linear Depths", svgf_push_constants.prev_frame_normals_and_linear_depths);
				execution_context.BlitImageStorageToTransient(
					svgf_push_constants.integrated_shadow_and_ao.y,
					"Denoised Raytraced Shadows and Ambient Occlusion"
				);

				// Swap to prepare for next frame
				std::swap(svgf_push_constants.integrated_shadow_and_ao.x, svgf_push_constants.integrated_shadow_and_ao.y);
			}
		);
	}

	render_graph.AddGraphicsPass("Composition Pass",
		{

			VkUtils::CreateTransientSampledImage("Albedo", VK_FORMAT_B8G8R8A8_UNORM, 0),
			VkUtils::CreateTransientSampledImage("World Space Normals and Linear Depths", VK_FORMAT_R16G16B16A16_SFLOAT, 1),
			VkUtils::CreateTransientSampledImage("Motion Vectors and Metallic Roughness", VK_FORMAT_R16G16B16A16_SFLOAT, 2),
			VkUtils::CreateTransientSampledImage("Depth", VK_FORMAT_D32_SFLOAT, 3),

			VkUtils::CreateTransientSampledImage("Shadow Map", 4096, 4096, VK_FORMAT_D32_SFLOAT, 4),
			VkUtils::CreateTransientSampledImage("Screen Space Ambient Occlusion", VK_FORMAT_R16G16B16A16_SFLOAT, 5),
			VkUtils::CreateTransientSampledImage("Screen Space Reflections", VK_FORMAT_R16G16B16A16_SFLOAT, 6),
			denoise_shadow_and_ao ?
				VkUtils::CreateTransientSampledImage("Denoised Raytraced Shadows and Ambient Occlusion", VK_FORMAT_R16G16B16A16_SFLOAT, 7) :
				VkUtils::CreateTransientSampledImage("Raytraced Shadows and Ambient Occlusion", VK_FORMAT_R16G16B16A16_SFLOAT, 7),
			VkUtils::CreateTransientSampledImage("Raytraced Reflections", VK_FORMAT_R16G16B16A16_SFLOAT, 8),
		},
		{
			VkUtils::CreateTransientRenderOutput(0)
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
					.specialization_constants = { 
						shadow_mode, 
						ambient_occlusion_mode,
						reflection_mode 
					}
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

void HybridRenderPath::DeregisterPath(VulkanContext& context, RenderGraph& render_graph, ResourceManager& resource_manager) {
	if(svgf_textures_created) {
		resource_manager.DestroyStorageImage(svgf_push_constants.integrated_shadow_and_ao.x);
		resource_manager.DestroyStorageImage(svgf_push_constants.integrated_shadow_and_ao.y);
		resource_manager.DestroyStorageImage(svgf_push_constants.prev_frame_normals_and_linear_depths);
		resource_manager.DestroyStorageImage(svgf_push_constants.shadow_and_ao_history);
		resource_manager.DestroyStorageImage(svgf_push_constants.shadow_and_ao_moments_history);
		svgf_textures_created = false;
	}
}

bool HybridRenderPath::ImGuiDrawSettings() {
	int old_shadow_mode = shadow_mode;
	int old_ambient_occlusion_mode = ambient_occlusion_mode;
	int old_reflection_mode = reflection_mode;
	bool old_denoise_shadow_and_ao = denoise_shadow_and_ao;

	ImGui::Text("Shadow Mode:");
	ImGui::RadioButton("Raytraced Shadows", &shadow_mode, SHADOW_MODE_RAYTRACED);
	ImGui::RadioButton("Rasterized Shadows", &shadow_mode, SHADOW_MODE_RASTERIZED);
	ImGui::RadioButton("No Shadows", &shadow_mode, SHADOW_MODE_OFF);
	ImGui::NewLine();

	ImGui::Text("Ambient Occlusion Mode:");
	ImGui::RadioButton("Raytraced Ambient Occlusion", &ambient_occlusion_mode, SHADOW_MODE_RAYTRACED);
	ImGui::RadioButton("Screen-Space Ambient Occlusion", &ambient_occlusion_mode, SHADOW_MODE_RASTERIZED);
	ImGui::RadioButton("No Ambient Occlusion", &ambient_occlusion_mode, SHADOW_MODE_OFF);
	ImGui::NewLine();
	ImGui::Checkbox("Denoise Shadows and Ambient Occlusion", &denoise_shadow_and_ao);
	ImGui::NewLine();
	ImGui::NewLine();

	ImGui::Text("Reflection Mode:");
	ImGui::RadioButton("Raytraced Reflections", &reflection_mode, REFLECTION_MODE_RAYTRACED);
	ImGui::RadioButton("Screen-Space Reflections", &reflection_mode, REFLECTION_MODE_SSR);
	ImGui::RadioButton("No Reflections", &reflection_mode, REFLECTION_MODE_OFF);
	ImGui::NewLine();

	if(old_shadow_mode != shadow_mode || 
	   old_ambient_occlusion_mode != ambient_occlusion_mode ||
	   old_reflection_mode != reflection_mode ||
	   old_denoise_shadow_and_ao!= denoise_shadow_and_ao) {
		return true;
	}

	return false;
}
