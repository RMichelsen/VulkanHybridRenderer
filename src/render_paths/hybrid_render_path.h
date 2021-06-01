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
	virtual void RegisterPath(VulkanContext& context, RenderGraph& render_graph, ResourceManager& resource_manager);
	virtual void DeregisterPath(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager);
	virtual void ImGuiDrawSettings();

private:
	int shadow_mode = 0;
	int ambient_occlusion_mode = 0;
	int reflection_mode = 0;
	bool denoise_shadow_and_ao = false;

	SVGFPushConstants svgf_push_constants;
	bool svgf_textures_created = false;

	SSRPushConstants ssr_push_constants;
	SSAOPushConstants ssao_push_constants;
};
