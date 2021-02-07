#pragma once

struct RenderGraphLayout {
	std::unordered_map<std::string, std::vector<std::string>> readers;
	std::unordered_map<std::string, std::vector<std::string>> writers;
	std::unordered_map<std::string, RenderPassDescription> render_pass_descriptions;
};

class ResourceManager;
class GraphicsPipelineExecutionContext {
public:
	GraphicsPipelineExecutionContext(VkCommandBuffer command_buffer,
		ResourceManager &resource_manager, RenderPass &render_pass, 
		GraphicsPipeline &pipeline) :
		command_buffer(command_buffer),
		resource_manager(resource_manager),
		render_pass(render_pass),
		pipeline(pipeline) {}

	void BindGlobalVertexAndIndexBuffers();
	void DrawIndexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, 
		uint32_t vertex_offset, uint32_t first_instance);
	void Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, 
		uint32_t first_instance);

	template<typename T>
	void PushConstants(T &push_constants) {
		assert(sizeof(T) == pipeline.description.push_constants.size);
		vkCmdPushConstants(command_buffer, pipeline.layout, pipeline.description.push_constants.pipeline_stages, 
			0, pipeline.description.push_constants.size, &push_constants);
	}

private:
	VkCommandBuffer command_buffer;
	ResourceManager &resource_manager;
	RenderPass &render_pass;
	GraphicsPipeline &pipeline;
};

class RaytracingPipelineExecutionContext {
public:
	RaytracingPipelineExecutionContext(VkCommandBuffer command_buffer,
		ResourceManager &resource_manager, RenderPass &render_pass, 
		RaytracingPipeline &pipeline) :
		command_buffer(command_buffer),
		resource_manager(resource_manager),
		render_pass(render_pass),
		pipeline(pipeline)
		{}

	void TraceRays(uint32_t width, uint32_t height);

private:
	VkCommandBuffer command_buffer;
	ResourceManager &resource_manager;
	RenderPass &render_pass;
	RaytracingPipeline &pipeline;
};

class VulkanContext;
class RenderGraph {
public:
	RenderGraph(VulkanContext &context);

	void AddGraphicsPass(const char *render_pass_name, std::vector<TransientResource> inputs,
		std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines,
		GraphicsPassCallback callback);
	void AddRaytracingPass(const char *render_pass_name, std::vector<TransientResource> inputs,
		std::vector<TransientResource> outputs, RaytracingPipelineDescription pipeline,
		RaytracingPassCallback callback);

	void Compile(ResourceManager &resource_manager);
	void Execute(ResourceManager &resource_manager, VkCommandBuffer command_buffer,
		uint32_t resource_idx, uint32_t image_idx);

private:
	void FindExecutionOrder();
	RenderPass CompileGraphicsPass(ResourceManager &resource_manager, 
		std::unordered_map<std::string, ImageAccess> &previous_access, RenderPassDescription &pass_description);
	RenderPass CompileRaytracingPass(ResourceManager &resource_manager,
		std::unordered_map<std::string, ImageAccess> &previous_access, RenderPassDescription &pass_description);
	void ExecuteGraphicsPass(ResourceManager &resource_manager, VkCommandBuffer command_buffer,
		uint32_t resource_idx, uint32_t image_idx, RenderPass &render_pass);
	void ExecuteRaytracingPass(ResourceManager &resource_manager, VkCommandBuffer command_buffer,
		RenderPass &render_pass);

	VulkanContext &context;
	std::vector<std::string> execution_order;
	std::unordered_map<std::string, ImageAccess> initial_image_access;
	std::vector<ImageLayoutTransition> finalize_transitions;

	RenderGraphLayout layout;
	std::unordered_map<std::string, RenderPass> render_passes;
	std::unordered_map<std::string, GraphicsPipeline> graphics_pipelines;
	std::unordered_map<std::string, RaytracingPipeline> raytracing_pipelines;
	std::unordered_map<std::string, Image> images;

};





