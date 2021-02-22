#pragma once

class VulkanContext;
class RenderGraph;
class ResourceManager;
namespace HybridShadowsInlineRaytracingRenderPath {
	void Enable(VulkanContext &context, ResourceManager &resource_manager, RenderGraph &render_graph);
}