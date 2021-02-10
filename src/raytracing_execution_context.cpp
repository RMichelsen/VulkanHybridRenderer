#include "pch.h"
#include "raytracing_execution_context.h"

void RaytracingExecutionContext::TraceRays(uint32_t width, uint32_t height) {
	VkStridedDeviceAddressRegionKHR raygen_sbt {
		.deviceAddress = pipeline.shader_binding_table_address + 0 * pipeline.shader_group_size,
		.stride = pipeline.shader_group_size,
		.size = pipeline.shader_group_size
	};
	VkStridedDeviceAddressRegionKHR miss_sbt {
		.deviceAddress = pipeline.shader_binding_table_address + 1 * pipeline.shader_group_size,
		.stride = pipeline.shader_group_size,
		.size = pipeline.shader_group_size
	};
	VkStridedDeviceAddressRegionKHR hit_sbt {
		.deviceAddress = pipeline.shader_binding_table_address + 2 * pipeline.shader_group_size,
		.stride = pipeline.shader_group_size,
		.size = pipeline.shader_group_size
	};
	VkStridedDeviceAddressRegionKHR callable_sbt {};

	// TODO: Move callback out of resource manager?
	vkCmdTraceRaysKHR(command_buffer, &raygen_sbt, &miss_sbt, &hit_sbt,
		&callable_sbt, width, height, 1);
}

