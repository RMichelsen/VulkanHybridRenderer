#pragma once
#include "render_path.h"

class RenderGraph;
class ResourceManager;
class RaytracedRenderPath : public RenderPath {
public:
	using RenderPath::RenderPath;
	virtual void RegisterPath(VulkanContext& context, RenderGraph& render_graph, ResourceManager& resource_manager);
	virtual void DeregisterPath(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager);
	virtual bool ImGuiDrawSettings();

private:
	int use_anyhit_shader = 0;
};
