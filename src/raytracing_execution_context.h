#pragma once

class ResourceManager;
class RaytracingExecutionContext {
public:
	RaytracingExecutionContext(VkCommandBuffer command_buffer, ResourceManager &resource_manager, 
		RaytracingPipeline &pipeline) :
		command_buffer(command_buffer),
		resource_manager(resource_manager),
		pipeline(pipeline)
	{}

	void TraceRays(uint32_t width, uint32_t height);

private:
	VkCommandBuffer command_buffer;
	ResourceManager &resource_manager;
	RaytracingPipeline &pipeline;
};

