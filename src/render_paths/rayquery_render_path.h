#pragma once
#include "render_path.h"

class RenderGraph;
class ResourceManager;
class RayqueryRenderPath : public RenderPath {
public:
	using RenderPath::RenderPath;
	virtual void RegisterPath(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager);
	virtual void ImGuiDrawSettings();
};
