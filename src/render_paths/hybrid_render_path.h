#pragma once
#include "render_path.h"

struct SVGFPushConstants {
	int prev_frame_reprojection_uv_and_object_id;
	int prev_frame_object_space_normals;
	int prev_frame_raytraced_shadows;
	int integrated_raytraced_shadows;
};

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
	virtual void AddPasses(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager);
	virtual void ImGuiDrawSettings();

private:
	int shadow_mode = 0;
	SVGFPushConstants svgf_push_constants;
};
