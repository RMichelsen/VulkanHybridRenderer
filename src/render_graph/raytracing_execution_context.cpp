#include "pch.h"
#include "raytracing_execution_context.h"

void RaytracingExecutionContext::TraceRays(uint32_t width, uint32_t height) {
	VkStridedDeviceAddressRegionKHR callable_sbt {};

	vkCmdTraceRaysKHR(command_buffer, 
		&pipeline.raygen_sbt.strided_device_address_region, 
		&pipeline.miss_sbt.strided_device_address_region,
		&pipeline.hit_sbt.strided_device_address_region,
		&callable_sbt, width, height, 1
	);
}

