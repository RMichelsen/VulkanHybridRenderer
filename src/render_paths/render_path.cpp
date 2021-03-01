#include "pch.h"
#include "render_path.h"

#include "render_graph/render_graph.h"
#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"

RenderPath::RenderPath(VulkanContext &context, RenderGraph &render_graph, ResourceManager &resource_manager) :
	context(context),
	render_graph(render_graph),
	resource_manager(resource_manager) {}

void RenderPath::Build() {
	render_graph.DestroyResources();
	AddPasses(context, render_graph, resource_manager);
	render_graph.Build();
}
