#pragma once
#include "render_path.h"

enum ShadowMode {
	SHADOW_MODE_RAYTRACED = 0,
	SHADOW_MODE_RASTERIZED = 1,
	SHADOW_MODE_OFF = 2
};

class RenderGraph;
class ResourceManager;
class HybridRenderPath : public RenderPath {
public:
	using RenderPath::RenderPath;
	virtual void RegisterPath(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager);
	virtual void ImGuiDrawSettings();

private:
	int shadow_mode = 0;
	bool denoise_shadows = false;

	SVGFPushConstants svgf_push_constants;
};
