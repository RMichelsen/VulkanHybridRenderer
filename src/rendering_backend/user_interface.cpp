#include "pch.h"
#include "user_interface.h"

#include "rendering_backend/pipeline.h"
#include "rendering_backend/renderer.h"
#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"
#include "render_graph/graphics_execution_context.h"
#include "render_graph/render_graph.h"
#include "render_paths/render_path.h"
#include "render_paths/hybrid_render_path.h"
#include "render_paths/rayquery_render_path.h"
#include "render_paths/raytraced_render_path.h"

struct ImGuiPushConstants {
	glm::vec2 scale;
	glm::vec2 translate;
	uint32_t texture;
};
inline constexpr uint32_t IMGUI_MAX_VERTEX_AND_INDEX_BUFSIZE = 32 * 1024 * 1024; // 32MB

UserInterface::UserInterface(VulkanContext &context, ResourceManager &resource_manager) : context(context) {
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize.x = static_cast<float>(context.swapchain.extent.width);
	io.DisplaySize.y = static_cast<float>(context.swapchain.extent.height);

	io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/consola.ttf", 20.0f);
	unsigned char *font_data;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&font_data, &width, &height);
	font_texture = resource_manager.UploadTextureFromData(width, height, font_data);
	io.Fonts->TexID = reinterpret_cast<ImTextureID>(static_cast<uint64_t>(font_texture));
	free(font_data);

	debug_textures[VK_FORMAT_B8G8R8A8_UNORM] = resource_manager.UploadEmptyTexture(4096, 4096, VK_FORMAT_B8G8R8A8_UNORM);
	debug_textures[VK_FORMAT_R8G8B8A8_UNORM] = resource_manager.UploadEmptyTexture(4096, 4096, VK_FORMAT_R8G8B8A8_UNORM);
	debug_textures[VK_FORMAT_D32_SFLOAT] = resource_manager.UploadEmptyTexture(4096, 4096, VK_FORMAT_D32_SFLOAT);
	debug_textures[VK_FORMAT_R16G16B16A16_SFLOAT] = resource_manager.UploadEmptyTexture(4096, 4096,VK_FORMAT_R16G16B16A16_SFLOAT);
	debug_textures[VK_FORMAT_R32G32B32A32_SFLOAT] = resource_manager.UploadEmptyTexture(4096, 4096, VK_FORMAT_R32G32B32A32_SFLOAT);
	debug_textures[VK_FORMAT_R16_SFLOAT] = resource_manager.UploadEmptyTexture(4096, 4096, VK_FORMAT_R16_SFLOAT);
	debug_textures[VK_FORMAT_R16G16_SFLOAT] = resource_manager.UploadEmptyTexture(4096, 4096, VK_FORMAT_R16G16_SFLOAT);
	active_debug_texture = debug_textures[VK_FORMAT_B8G8R8A8_UNORM];

	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(IMGUI_MAX_VERTEX_AND_INDEX_BUFSIZE, 
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	for(MappedBuffer &buffer : vertex_buffers) {
		buffer = VkUtils::CreateMappedBuffer(context.allocator, buffer_info);
	}
	buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	for(MappedBuffer &buffer : index_buffers) {
		buffer = VkUtils::CreateMappedBuffer(context.allocator, buffer_info);
	}

	CreateImGuiRenderPass();
	CreateImGuiPipeline(resource_manager);

	QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&performance_frequency));
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&global_time));
}

void UserInterface::DestroyResources() {
	VK_CHECK(vkDeviceWaitIdle(context.device));

	for(MappedBuffer &buffer : vertex_buffers) {
		VkUtils::DestroyMappedBuffer(context.allocator, buffer);
	}
	for(MappedBuffer &buffer : index_buffers) {
		VkUtils::DestroyMappedBuffer(context.allocator, buffer);
	}

	for(VkFramebuffer &framebuffer : framebuffers) {
		vkDestroyFramebuffer(context.device, framebuffer, nullptr);
	}
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyRenderPass(context.device, render_pass, nullptr);
}

UserInterfaceState UserInterface::Update(RenderGraph &render_graph,
	RenderPath &active_render_path, std::vector<std::string> current_color_attachments) {
	ImGuiIO &io = ImGui::GetIO();
	POINT p;
	GetCursorPos(&p);
	ScreenToClient(context.hwnd, &p);
	io.MousePos.x = static_cast<float>(p.x);
	io.MousePos.y = static_cast<float>(p.y);

	uint64_t current_time = 0;
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&current_time));
	io.DeltaTime = (float)(current_time - global_time) / performance_frequency;
	global_time = current_time;

	ImGui::NewFrame();

	ImGui::BeginMainMenuBar();
	static std::string current_texture = "";
	RenderPathState render_path_state = RenderPathState::Idle;
	if(ImGui::BeginMenu("Render Paths"))
	{
		if(ImGui::MenuItem("Hybrid Render Path")) {
			render_path_state = RenderPathState::ChangeToHybrid;
			current_texture = "";
		}
		if(ImGui::MenuItem("Rayquery Render Path")) {
			render_path_state = RenderPathState::ChangeToRayquery;
			current_texture = "";
		}
		if(ImGui::MenuItem("Raytraced Render Path")) {
			render_path_state = RenderPathState::ChangeToRaytraced;
			current_texture = "";
		}
		if(ImGui::MenuItem("Forward Rasterized Render Path")) {
			render_path_state = RenderPathState::ChangeToForwardRaster;
			current_texture = "";
		}
		ImGui::EndMenu();
	}
	ImGui::EndMainMenuBar();

	render_graph.DrawPerformanceStatistics();

	ImGui::Begin("Render Path Configuration");
	bool needs_rebuild = active_render_path.ImGuiDrawSettings() || (render_path_state != RenderPathState::Idle);
	ImGui::End();

	ImGui::Begin("Debug Texture");
	if(ImGui::BeginCombo("##texture_combo", current_texture.c_str()))
	{
		for(std::string &texture : current_color_attachments) {
			bool is_selected = texture == current_texture;
			if(ImGui::Selectable(texture.c_str(), is_selected)) {
				current_texture = texture;
			}
		}
		ImGui::EndCombo();
	}

	if(!current_texture.empty()) {
		ImGui::Image(
			reinterpret_cast<ImTextureID>(static_cast<uint64_t>(active_debug_texture)),
			ImGui::GetContentRegionAvail(),
			ImVec2(0, io.DisplaySize.y / 4096),
			ImVec2(io.DisplaySize.x / 4096, 0) 
		);
	}
	ImGui::End();

	ImGui::EndFrame();
	ImGui::Render();

	return UserInterfaceState {
		.render_path_state = render_path_state,
		.render_path_needs_rebuild = needs_rebuild,
		.debug_texture = current_texture
	};
}

void UserInterface::Draw(ResourceManager &resource_manager, VkCommandBuffer command_buffer, 
	uint32_t resource_idx, uint32_t image_idx) {
	VkFramebuffer& framebuffer = framebuffers[resource_idx];

	// Delete previous framebuffer
	if(framebuffer != VK_NULL_HANDLE) {
		vkDestroyFramebuffer(context.device, framebuffer, nullptr);
		framebuffer = VK_NULL_HANDLE;
	}
	VkFramebufferCreateInfo framebuffer_info {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = render_pass,
		.attachmentCount = 1,
		.pAttachments = &context.swapchain.image_views[image_idx],
		.width = context.swapchain.extent.width,
		.height = context.swapchain.extent.height,
		.layers = 1
	};
	VK_CHECK(vkCreateFramebuffer(context.device, &framebuffer_info, nullptr, &framebuffer));

	ImDrawData *draw_data = ImGui::GetDrawData();
	if(!draw_data || draw_data->CmdListsCount == 0) {
		return;
	}

	VkDeviceSize vertex_buffer_size = draw_data->TotalVtxCount * sizeof(ImGuiVertex);
	VkDeviceSize index_buffer_size = draw_data->TotalIdxCount * sizeof(uint16_t);
	if(vertex_buffer_size == 0 || index_buffer_size == 0) {
		return;
	}

	assert(vertex_buffer_size < IMGUI_MAX_VERTEX_AND_INDEX_BUFSIZE);
	assert(index_buffer_size < IMGUI_MAX_VERTEX_AND_INDEX_BUFSIZE);

	ImGuiVertex *vertex_data = reinterpret_cast<ImGuiVertex *>(vertex_buffers[resource_idx].mapped_data);
	uint16_t *index_data = reinterpret_cast<uint16_t *>(index_buffers[resource_idx].mapped_data);

	for(int i = 0; i < draw_data->CmdListsCount; ++i) {
		ImDrawList *draw_list = draw_data->CmdLists[i];
		memcpy(vertex_data, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImGuiVertex));
		memcpy(index_data, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(uint16_t));
		vertex_data += draw_list->VtxBuffer.Size;
		index_data += draw_list->IdxBuffer.Size;
	}

	VkRenderPassBeginInfo render_pass_begin_info {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = render_pass,
		.framebuffer = framebuffer,
		.renderArea = VkRect2D {
			.offset = VkOffset2D {.x = 0, .y = 0 },
			.extent = context.swapchain.extent
		}
	};
	vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline_layout, 0, 1, &resource_manager.global_descriptor_set0, 0, nullptr);

	ImGuiPushConstants push_constants {
		.scale = glm::vec2 { 
			2.0f / draw_data->DisplaySize.x, 
			2.0f / draw_data->DisplaySize.y
		},
		.translate = glm::vec2 { -1.0f, -1.0f },
		.texture = font_texture
	};

	VkViewport viewport {
		.width = draw_data->DisplaySize.x,
		.height = draw_data->DisplaySize.y,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);

	std::array<VkDeviceSize, 1> offsets = { 0 };
	uint32_t vertex_offset = 0;
	uint32_t index_offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffers[resource_idx].handle, offsets.data());
	vkCmdBindIndexBuffer(command_buffer, index_buffers[resource_idx].handle, 0, VK_INDEX_TYPE_UINT16);
	for(int i = 0; i < draw_data->CmdListsCount; ++i) {
		ImDrawList *draw_list = draw_data->CmdLists[i];
		for(int j = 0; j < draw_list->CmdBuffer.Size; ++j) {
			ImDrawCmd &cmd = draw_list->CmdBuffer[j];

			push_constants.texture = static_cast<uint32_t>(reinterpret_cast<uint64_t>(cmd.TextureId));
			vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | 
				VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ImGuiPushConstants), &push_constants);

			VkRect2D scissor_rect {
				.offset = VkOffset2D {
					.x = std::max(static_cast<int>(cmd.ClipRect.x), 0),
					.y = std::max(static_cast<int>(cmd.ClipRect.y), 0)
				},
				.extent = VkExtent2D {
					.width = static_cast<uint32_t>(cmd.ClipRect.z - cmd.ClipRect.x),
					.height = static_cast<uint32_t>(cmd.ClipRect.w - cmd.ClipRect.y)
				}
			};
			vkCmdSetScissor(command_buffer, 0, 1, &scissor_rect);
			vkCmdDrawIndexed(command_buffer, cmd.ElemCount, 1, index_offset, vertex_offset, 0);
			index_offset += cmd.ElemCount;
		}
		vertex_offset += draw_list->VtxBuffer.Size;
	}

	vkCmdEndRenderPass(command_buffer);
}

uint32_t UserInterface::SetActiveDebugTexture(VkFormat format) {
	assert(debug_textures.contains(format));
	active_debug_texture = debug_textures[format];
	return active_debug_texture;
}

void UserInterface::ResizeToSwapchain() {
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize.x = static_cast<float>(context.swapchain.extent.width);
	io.DisplaySize.y = static_cast<float>(context.swapchain.extent.height);
}

void UserInterface::MouseLeftButtonDown() {
	ImGui::GetIO().MouseDown[0] = true;
}

void UserInterface::MouseLeftButtonUp() {
	ImGui::GetIO().MouseDown[0] = false;
}

void UserInterface::MouseRightButtonDown() {
	ImGui::GetIO().MouseDown[1] = true;
}

void UserInterface::MouseRightButtonUp() {
	ImGui::GetIO().MouseDown[1] = false;
}

void UserInterface::MouseScroll(float delta) {
	ImGui::GetIO().MouseWheel += delta;
}

void UserInterface::CreateImGuiRenderPass() {
	std::array<VkAttachmentDescription, 1> attachments {
	VkAttachmentDescription {
		.format = context.swapchain.format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	}
	};
	std::array<VkAttachmentReference, 1> color_attachment_refs {
		VkAttachmentReference {
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		}
	};
	VkSubpassDescription subpass_description {
		.colorAttachmentCount = static_cast<uint32_t>(color_attachment_refs.size()),
		.pColorAttachments = color_attachment_refs.data()
	};
	VkSubpassDependency subpass_dependency {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};
	VkRenderPassCreateInfo render_pass_info {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = static_cast<uint32_t>(attachments.size()),
		.pAttachments = attachments.data(),
		.subpassCount = 1,
		.pSubpasses = &subpass_description,
		.dependencyCount = 1,
		.pDependencies = &subpass_dependency
	};
	vkCreateRenderPass(context.device, &render_pass_info, nullptr, &render_pass);
}

void UserInterface::CreateImGuiPipeline(ResourceManager &resource_manager) {
	std::array<VkPipelineShaderStageCreateInfo, 2> shader_stage_infos {
		VkUtils::PipelineShaderStageCreateInfo(context.device, "imgui/imgui.vert",
			VK_SHADER_STAGE_VERTEX_BIT),
		VkUtils::PipelineShaderStageCreateInfo(context.device, "imgui/imgui.frag",
			VK_SHADER_STAGE_FRAGMENT_BIT)
	};

	VkPipelineVertexInputStateCreateInfo vertex_input_state_info = VERTEX_INPUT_STATE_IMGUI;
	VkPipelineInputAssemblyStateCreateInfo input_assembly_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};
	VkViewport viewport {
		.width = static_cast<float>(context.swapchain.extent.width),
		.height = static_cast<float>(context.swapchain.extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	VkRect2D scissor {
		.extent = context.swapchain.extent
	};
	VkPipelineViewportStateCreateInfo viewport_state_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &viewport,
		.scissorCount = 1,
		.pScissors = &scissor
	};

	std::array<VkPushConstantRange, 1> push_constants {
		VkPushConstantRange {
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0,
			.size = sizeof(ImGuiPushConstants)
		}
	};

	std::array<VkDescriptorSetLayout, 1> descriptor_set_layouts {
		resource_manager.global_descriptor_set_layout0
	};
	VkPipelineLayoutCreateInfo layout_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = static_cast<uint32_t>(descriptor_set_layouts.size()),
		.pSetLayouts = descriptor_set_layouts.data(),
		.pushConstantRangeCount = static_cast<uint32_t>(push_constants.size()),
		.pPushConstantRanges = push_constants.empty() ? nullptr : push_constants.data()
	};

	VK_CHECK(vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout));

	std::array<VkPipelineColorBlendAttachmentState, 1> color_blend_states {
		COLOR_BLEND_ATTACHMENT_STATE_IMGUI
	};
	VkPipelineColorBlendStateCreateInfo color_blend_state {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = static_cast<uint32_t>(color_blend_states.size()),
		.pAttachments = color_blend_states.data()
	};
	VkGraphicsPipelineCreateInfo pipeline_info {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = static_cast<uint32_t>(shader_stage_infos.size()),
		.pStages = shader_stage_infos.data(),
		.pVertexInputState = &vertex_input_state_info,
		.pInputAssemblyState = &input_assembly_state_info,
		.pViewportState = &viewport_state_info,
		.pRasterizationState = &RASTERIZATION_STATE_CULL_NONE,
		.pMultisampleState = &MULTISAMPLE_STATE_OFF,
		.pDepthStencilState = &DEPTH_STENCIL_STATE_OFF,
		.pColorBlendState = &color_blend_state,
		.pDynamicState = &DYNAMIC_STATE_VIEWPORT_SCISSOR,
		.layout = pipeline_layout,
		.renderPass = render_pass,
		.subpass = 0
	};

	VK_CHECK(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
		nullptr, &pipeline));

	for(VkPipelineShaderStageCreateInfo& pipeline_shader_stage_info : shader_stage_infos) {
		vkDestroyShaderModule(context.device, pipeline_shader_stage_info.module, nullptr);
	}
}

