#pragma once

class ResourceManager;
class GraphicsExecutionContext {
public:
	GraphicsExecutionContext(VkCommandBuffer command_buffer, ResourceManager &resource_manager,
		GraphicsPipeline &pipeline) :
		command_buffer(command_buffer),
		resource_manager(resource_manager),
		pipeline(pipeline) {}

	void BindGlobalVertexAndIndexBuffers();
	void BindVertexBuffer(VkBuffer buffer, VkDeviceSize offset);
	void BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type);
	void SetScissor(VkRect2D scissor);
	void SetViewport(VkViewport viewport);
	void DrawIndexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index,
		uint32_t vertex_offset, uint32_t first_instance);
	void Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
		uint32_t first_instance);

	template<typename T>
	void PushConstants(T &push_constants) {
		assert(sizeof(T) == pipeline.description.push_constants.size);
		vkCmdPushConstants(command_buffer, pipeline.layout, pipeline.description.push_constants.pipeline_stage,
			0, pipeline.description.push_constants.size, &push_constants);
	}

private:
	VkCommandBuffer command_buffer;
	ResourceManager &resource_manager;
	GraphicsPipeline &pipeline;
};

