#pragma once

class VulkanContext;
class RenderGraph;
class ResourceManager;
namespace DeferredRenderPath {
void Enable(VulkanContext &context, ResourceManager &resource_manager, RenderGraph &render_graph);
}
