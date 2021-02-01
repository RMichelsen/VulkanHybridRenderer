#pragma once

struct RenderGraphLayout {
	std::unordered_map<std::string, std::vector<std::string>> readers;
	std::unordered_map<std::string, std::vector<std::string>> writers;
	std::unordered_map<std::string, TransientResource> transient_resources;
	std::unordered_map<std::string, RenderPassDescription> render_pass_descriptions;
};

class VulkanContext;
class ResourceManager;
class RenderGraph {
public:
	RenderGraph(VulkanContext &context);

	void AddGraphicsPass(const char *render_pass_name, std::vector<TransientResource> inputs,
		std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines,
		std::function<void(RenderPass &render_pass, std::unordered_map<std::string, GraphicsPipeline> &pipelines, VkCommandBuffer &command_buffer)> executor);

	void Compile(ResourceManager &resource_manager);
	void Execute(VkCommandBuffer &command_buffer, uint32_t resource_idx, uint32_t image_idx);

private:
	void FindExecutionOrder();
	void ActualizeTransientResource(ResourceManager &resource_manager, TransientResource &resource);

	VulkanContext &context;

	std::vector<std::string> execution_order;

	RenderGraphLayout layout;
	std::unordered_map<std::string, RenderPass> render_passes;
	std::unordered_map<std::string, GraphicsPipeline> graphics_pipelines;
	std::unordered_map<std::string, RaytracingPipeline> raytracing_pipelines;
	std::unordered_map<std::string, Texture> textures;
};


