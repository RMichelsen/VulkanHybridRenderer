#include "pch.h"
#include "compute_execution_context.h"

#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"

glm::uvec2 ComputeExecutionContext::GetDisplaySize() {
	return { render_graph.context.swapchain.extent.width, render_graph.context.swapchain.extent.height };
}

void ComputeExecutionContext::Dispatch(const char *entry, uint32_t x_groups, uint32_t y_groups, uint32_t z_groups) {
	ComputePipeline &pipeline = render_graph.compute_pipelines[entry];

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline.layout, 0, 1, &resource_manager.global_descriptor_set0, 0, nullptr);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline.layout, 1, 1, &resource_manager.global_descriptor_set1, 0, nullptr);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline.layout, 2, 1, &resource_manager.per_frame_descriptor_set, 0, nullptr);

	if(render_pass.descriptor_set != VK_NULL_HANDLE) {
		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout,
			3, 1, &render_pass.descriptor_set, 0, nullptr);
	}

	vkCmdDispatch(command_buffer, x_groups, y_groups, z_groups);
}
