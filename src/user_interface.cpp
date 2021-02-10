#include "pch.h"
#include "user_interface.h"

#include "graphics_execution_context.h"
#include "render_graph.h"
#include "resource_manager.h"
#include "vulkan_context.h"
#include "vulkan_utils.h"

inline constexpr uint32_t IMGUI_MAX_VERTEX_AND_INDEX_BUFSIZE = 32 * 1024 * 1024; // 32MB

UserInterface::UserInterface(VulkanContext &context, 
	ResourceManager &resource_manager) : context(context) {
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize.x = static_cast<float>(context.swapchain.extent.width);
	io.DisplaySize.y = static_cast<float>(context.swapchain.extent.height);

	io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/calibri.ttf", 20.0f);
	unsigned char *font_data;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&font_data, &width, &height);
	font_texture = resource_manager.UploadTextureFromData(width, height, font_data);
	io.Fonts->TexID = reinterpret_cast<ImTextureID>(static_cast<uint64_t>(font_texture));

	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(IMGUI_MAX_VERTEX_AND_INDEX_BUFSIZE, 
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	vertex_buffer = VkUtils::CreateMappedBuffer(context.allocator, buffer_info);
	buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	index_buffer = VkUtils::CreateMappedBuffer(context.allocator, buffer_info);
}

void UserInterface::DestroyResources() {
	VK_CHECK(vkDeviceWaitIdle(context.device));
	VkUtils::DestroyMappedBuffer(context.allocator, vertex_buffer);
	VkUtils::DestroyMappedBuffer(context.allocator, index_buffer);
}

void UserInterface::Update() {
	ImGui::NewFrame();
	ImGui::ShowDemoWindow();
	ImGui::EndFrame();

	ImGui::Render();
}

void UserInterface::Draw(GraphicsExecutionContext &execution_context) {
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

	ImGuiVertex *vertex_data = reinterpret_cast<ImGuiVertex *>(vertex_buffer.mapped_data);
	uint16_t *index_data = reinterpret_cast<uint16_t *>(index_buffer.mapped_data);

	for(int i = 0; i < draw_data->CmdListsCount; ++i) {
		ImDrawList *draw_list = draw_data->CmdLists[i];
		memcpy(vertex_data, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImGuiVertex));
		memcpy(index_data, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(uint16_t));
		vertex_data += draw_list->VtxBuffer.Size;
		index_data += draw_list->IdxBuffer.Size;
	}

	ImGuiPushConstants push_constants {
		.scale = glm::vec2 { 
			2.0f / draw_data->DisplaySize.x, 
			2.0f / draw_data->DisplaySize.y
		},
		.translate = glm::vec2 { -1.0f, -1.0f },
		.font_texture = font_texture
	};
	execution_context.PushConstants(push_constants);

	VkViewport viewport {
		.width = draw_data->DisplaySize.x,
		.height = draw_data->DisplaySize.y,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	execution_context.SetViewport(viewport);

	uint32_t vertex_offset = 0;
	uint32_t index_offset = 0;
	execution_context.BindVertexBuffer(vertex_buffer.handle, 0);
	execution_context.BindIndexBuffer(index_buffer.handle, 0, VK_INDEX_TYPE_UINT16);
	for(int i = 0; i < draw_data->CmdListsCount; ++i) {
		ImDrawList *draw_list = draw_data->CmdLists[i];
		for(int j = 0; j < draw_list->CmdBuffer.Size; ++j) {
			ImDrawCmd &cmd = draw_list->CmdBuffer[j];
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
			execution_context.SetScissor(scissor_rect);


			execution_context.DrawIndexed(cmd.ElemCount, 1, index_offset, vertex_offset, 0);
			index_offset += cmd.ElemCount;
		}
		vertex_offset += draw_list->VtxBuffer.Size;
	}
}

