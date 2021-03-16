#pragma once
#include "render_path.h"

class RenderGraph;
class ResourceManager;
class ForwardRasterRenderPath : public RenderPath {
public:
	using RenderPath::RenderPath;
	virtual void AddPasses(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager);
	virtual void ImGuiDrawSettings();
};
