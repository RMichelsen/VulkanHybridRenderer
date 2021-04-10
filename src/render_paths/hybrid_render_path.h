#pragma once
#include "render_path.h"

enum ShadowMode {
	SHADOW_MODE_RAYTRACED = 0,
	SHADOW_MODE_RASTERIZED = 1,
	SHADOW_MODE_OFF = 2
};

enum AmbientOcclusionMode {
	AMBIENT_OCCLUSION_MODE_RAYTRACED = 0,
	AMBIENT_OCCLUSION_MODE_SSAO = 1,
	AMBIENT_OCCLUSION_MODE_OFF = 2
};

enum ReflectionMode {
	REFLECTION_MODE_RAYTRACED = 0,
	REFLECTION_MODE_SSR = 1,
	REFLECTION_MODE_OFF = 2
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
	int ambient_occlusion_mode = 0;
	int reflection_mode = 0;
	bool denoise_shadows = true;
	bool denoise_ambient_occlusion = true;
	bool denoise_reflections = true;

	SVGFPushConstants svgf_push_constants;
};
