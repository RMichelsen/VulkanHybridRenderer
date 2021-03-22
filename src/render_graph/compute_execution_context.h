#pragma once
#include "render_graph/render_graph.h"

class RenderGraph;
class ResourceManager;
class ComputeExecutionContext {
public:
	ComputeExecutionContext(VkCommandBuffer command_buffer, RenderPass &render_pass,
		RenderGraph &render_graph,
		ResourceManager &resource_manager) :
		command_buffer(command_buffer),
		render_pass(render_pass),
		render_graph(render_graph),
		resource_manager(resource_manager)
	{}

	glm::uvec2 GetDisplaySize();
	void Dispatch(const char *entry, uint32_t x_groups, uint32_t y_groups, uint32_t z_groups);

	template<typename T>
	void PushConstants(const char *entry, T &push_constants) {
		ComputePipeline &pipeline = render_graph.compute_pipelines[entry];
		
		assert(sizeof(T) == pipeline.push_constant_description.size);
		vkCmdPushConstants(command_buffer, pipeline.layout, pipeline.push_constant_description.shader_stage,
			0, pipeline.push_constant_description.size, &push_constants);
	}

private:
	VkCommandBuffer command_buffer;
	RenderPass &render_pass;
	RenderGraph &render_graph;
	ResourceManager &resource_manager;
};

