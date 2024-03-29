#include "pch.h"
#include "graphics_execution_context.h"

#include "rendering_backend/resource_manager.h"

void GraphicsExecutionContext::BindGlobalVertexAndIndexBuffers() {
	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &resource_manager.global_vertex_buffer.handle, &offset);
	vkCmdBindIndexBuffer(command_buffer, resource_manager.global_index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
}

void GraphicsExecutionContext::BindVertexBuffer(VkBuffer buffer, VkDeviceSize offset) {
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &buffer, &offset);
}

void GraphicsExecutionContext::BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type) {
	vkCmdBindIndexBuffer(command_buffer, buffer, offset, type);
}

void GraphicsExecutionContext::SetScissor(VkRect2D scissor) {
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);
}

void GraphicsExecutionContext::SetViewport(VkViewport viewport) {
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);
}

void GraphicsExecutionContext::SetDepthBias(float depth_bias_constant_factor, float depth_bias_clamp, 
	float depth_bias_slope_factor) {
	vkCmdSetDepthBias(command_buffer, depth_bias_constant_factor, depth_bias_clamp, depth_bias_slope_factor);
}

void GraphicsExecutionContext::DrawIndexed(uint32_t index_count, uint32_t instance_count,
	uint32_t first_index, uint32_t vertex_offset, uint32_t first_instance) {
	vkCmdDrawIndexed(command_buffer, index_count, instance_count, first_index, vertex_offset, first_instance);
}

void GraphicsExecutionContext::Draw(uint32_t vertex_count, uint32_t instance_count,
	uint32_t first_vertex, uint32_t first_instance) {
	vkCmdDraw(command_buffer, vertex_count, instance_count, first_vertex, instance_count);
}
