#pragma once

class VulkanContext;
class RenderGraph;
class ResourceManager;
class RenderPath {
public:
	RenderPath(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager);
	void Build();

	virtual void AddPasses(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager) = 0;
	virtual void ImGuiDrawSettings() = 0;

private:
	VulkanContext &context;
	RenderGraph &render_graph;
	ResourceManager &resource_manager;
};

