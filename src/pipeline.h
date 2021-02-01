#pragma once

class VulkanContext;
class ResourceManager;
namespace VkUtils {
GraphicsPipeline CreateGraphicsPipeline(VulkanContext &context, ResourceManager &resource_manager,
	RenderPass render_pass, GraphicsPipelineDescription description);
RaytracingPipeline CreateRaytracingPipeline(VulkanContext &context, ResourceManager &resource_manager,
	RaytracingPipelineDescription description);
}
