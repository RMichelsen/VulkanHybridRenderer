#pragma once

class VulkanContext;
class RenderGraph;
class ResourceManager;
namespace RasterizedShadowsRenderPath {
	void Enable(VulkanContext &context, ResourceManager &resource_manager, RenderGraph &render_graph);
}