#pragma once

class VulkanContext;
class RenderGraph;
class ResourceManager;
class RenderPath {
public:
	RenderPath(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager);
	void Build();
	void Rebuild();

	virtual void RegisterPath(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager) = 0;
	virtual void DeregisterPath(VulkanContext& context, RenderGraph& render_graph, ResourceManager& resource_manager) = 0;
	virtual void ImGuiDrawSettings() = 0;

private:
	VulkanContext &context;
	RenderGraph &render_graph;
	ResourceManager &resource_manager;
};

