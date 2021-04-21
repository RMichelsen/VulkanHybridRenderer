#include "pch.h"
#include "render_path.h"

#include "render_graph/render_graph.h"
#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"

RenderPath::RenderPath(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager) :
	context(context),
	render_graph(render_graph),
	resource_manager(resource_manager) {}

void RenderPath::Build() {
	VK_CHECK(vkDeviceWaitIdle(context.device));

	render_graph.DestroyResources();
	RegisterPath(context, render_graph, resource_manager);
	render_graph.Build();
}

void RenderPath::Rebuild() {
	VK_CHECK(vkDeviceWaitIdle(context.device));

	DeregisterPath(context, render_graph, resource_manager);
	Build();
}
