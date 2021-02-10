#pragma once

class VulkanContext;
class ResourceManager;
class RenderGraph {
public:
	RenderGraph(VulkanContext &context, ResourceManager &resource_manager);
	void DestroyResources();

	void AddGraphicsPass(const char *render_pass_name, std::vector<TransientResource> dependencies,
		std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines,
		GraphicsPassCallback callback);
	void AddRaytracingPass(const char *render_pass_name, std::vector<TransientResource> dependencies,
		std::vector<TransientResource> outputs, RaytracingPipelineDescription pipeline,
		RaytracingPassCallback callback);

	void Build();
	void Execute(VkCommandBuffer command_buffer, uint32_t resource_idx, uint32_t image_idx);

private:
	void CreateGraphicsPass(RenderPassDescription &pass_description);
	void CreateRaytracingPass(RenderPassDescription &pass_description);

	void FindExecutionOrder();
	void InsertBarriers(VkCommandBuffer command_buffer, RenderPass &render_pass);
	void ExecuteGraphicsPass(VkCommandBuffer command_buffer, uint32_t resource_idx, uint32_t image_idx, RenderPass &render_pass);
	void ExecuteRaytracingPass(VkCommandBuffer command_buffer, RenderPass &render_pass);
	void ActualizeResource(TransientResource &resource);
	bool SanityCheck();

	VulkanContext &context;
	ResourceManager &resource_manager;

	std::vector<std::string> execution_order;
	std::unordered_map<std::string, std::vector<std::string>> readers;
	std::unordered_map<std::string, std::vector<std::string>> writers;
	std::unordered_map<std::string, RenderPassDescription> pass_descriptions;
	std::unordered_map<std::string, RenderPass> passes;
	std::unordered_map<std::string, GraphicsPipeline> graphics_pipelines;
	std::unordered_map<std::string, RaytracingPipeline> raytracing_pipelines;
	std::unordered_map<std::string, Image> images;
	std::unordered_map<std::string, ImageAccess> image_access;
};

