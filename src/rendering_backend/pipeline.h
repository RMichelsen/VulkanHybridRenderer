#pragma once

class VulkanContext;
class ResourceManager;
namespace VkUtils {
GraphicsPipeline CreateGraphicsPipeline(VulkanContext &context, ResourceManager &resource_manager,
	RenderPass &render_pass, GraphicsPipelineDescription description);
RaytracingPipeline CreateRaytracingPipeline(VulkanContext &context, ResourceManager &resource_manager,
	RenderPass &render_pass, RaytracingPipelineDescription description, 
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR &raytracing_properties);
ComputePipeline CreateComputePipeline(VulkanContext &context, ResourceManager &resource_manager,
	RenderPass &render_pass, PushConstantDescription push_constant_description, ComputeKernel kernel);
}

