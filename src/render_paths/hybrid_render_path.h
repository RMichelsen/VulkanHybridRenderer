#pragma once
#include "render_path.h"

struct SVGFPushConstants {
	int prev_frame_reprojection_uv_and_object_id;
	int prev_frame_object_space_normals;
	int shadow_history;
	int atrous_step;

	// Pingpong texture
	glm::ivec2 integrated_shadows;
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
	virtual void RegisterPath(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager);
	virtual void ImGuiDrawSettings();

private:
	int shadow_mode = 0;
	bool denoise_shadows = false;

	SVGFPushConstants svgf_push_constants;
};
