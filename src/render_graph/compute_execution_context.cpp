#include "pch.h"
#include "compute_execution_context.h"

#include "rendering_backend/resource_manager.h"
#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"

glm::uvec2 ComputeExecutionContext::GetDisplaySize() {
	return { render_graph.context.swapchain.extent.width, render_graph.context.swapchain.extent.height };
}

void ComputeExecutionContext::Dispatch(const char *shader, uint32_t x_groups, uint32_t y_groups, uint32_t z_groups) {
	ComputePipeline &pipeline = render_graph.compute_pipelines[shader];

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline.layout, 0, 1, &resource_manager.global_descriptor_set0, 0, nullptr);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline.layout, 1, 1, &resource_manager.global_descriptor_set1, 0, nullptr);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline.layout, 2, 1, &resource_manager.per_frame_descriptor_sets[resource_idx], 0, nullptr);

	if(render_pass.descriptor_set != VK_NULL_HANDLE) {
		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout,
			3, 1, &render_pass.descriptor_set, 0, nullptr);
	}

	vkCmdDispatch(command_buffer, x_groups, y_groups, z_groups);
}

void ComputeExecutionContext::BlitImageStorageToTransient(int src, const char *dst) {
	Image src_image = resource_manager.storage_images[src];
	Image dst_image = render_graph.images[dst];
	assert(!VkUtils::IsDepthFormat(src_image.format) && !VkUtils::IsDepthFormat(dst_image.format));

	VkUtils::InsertImageBarrier(
		command_buffer,
		src_image.handle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_ACCESS_TRANSFER_READ_BIT
	);
	VkImageLayout dst_old_layout = render_graph.image_access[dst].layout;
	VkUtils::InsertImageBarrier(
		command_buffer,
		dst_image.handle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		dst_old_layout,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		render_graph.image_access[dst].stage_flags,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		render_graph.image_access[dst].access_flags,
		VK_ACCESS_TRANSFER_WRITE_BIT
	);
	BlitImage(src_image, dst_image);
	VkUtils::InsertImageBarrier(
		command_buffer,
		src_image.handle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_READ_BIT,
		VK_ACCESS_SHADER_READ_BIT
	);
	render_graph.image_access[dst] = ImageAccess {
		.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.access_flags = VK_ACCESS_TRANSFER_WRITE_BIT,
		.stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT
	};
}

void ComputeExecutionContext::BlitImageTransientToStorage(const char *src, int dst) {
	Image src_image = render_graph.images[src];
	Image dst_image = resource_manager.storage_images[dst];
	assert(!VkUtils::IsDepthFormat(src_image.format) && !VkUtils::IsDepthFormat(dst_image.format));

	VkImageLayout src_old_layout = render_graph.image_access[src].layout;
	VkUtils::InsertImageBarrier(
		command_buffer,
		src_image.handle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		src_old_layout,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		render_graph.image_access[src].stage_flags,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		render_graph.image_access[src].access_flags,
		VK_ACCESS_TRANSFER_READ_BIT
	);
	VkUtils::InsertImageBarrier(
		command_buffer,
		dst_image.handle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT
	);
	BlitImage(src_image, dst_image);
	render_graph.image_access[src] = ImageAccess {
		.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.access_flags = VK_ACCESS_TRANSFER_READ_BIT,
		.stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT
	};
	VkUtils::InsertImageBarrier(
		command_buffer,
		dst_image.handle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_SHADER_READ_BIT
	);
}
 
void ComputeExecutionContext::BlitImageStorageToStorage(int src, int dst) {
	Image src_image = resource_manager.storage_images[src];
	Image dst_image = resource_manager.storage_images[dst];
	assert(src_image.width == dst_image.width);
	assert(src_image.height == dst_image.height);

	VkUtils::InsertImageBarrier(
		command_buffer,
		src_image.handle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_ACCESS_TRANSFER_READ_BIT
	);
	VkUtils::InsertImageBarrier(
		command_buffer,
		dst_image.handle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT
	);
	BlitImage(src_image, dst_image);
	VkUtils::InsertImageBarrier(
		command_buffer,
		src_image.handle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_READ_BIT,
		VK_ACCESS_SHADER_READ_BIT
	);
	VkUtils::InsertImageBarrier(
		command_buffer,
		dst_image.handle,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_SHADER_READ_BIT
	);
}

void ComputeExecutionContext::BlitImage(Image src, Image dst) {
	assert(src.width == dst.width);
	assert(src.height == dst.height);

	VkImageBlit image_blit {
		.srcSubresource = VkImageSubresourceLayers {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.layerCount = 1
		},
		.srcOffsets = {
			VkOffset3D {},
			VkOffset3D {.x = static_cast<int>(src.width), .y = static_cast<int>(src.height), .z = 1}
		},
		.dstSubresource = VkImageSubresourceLayers {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.layerCount = 1
		},
		.dstOffsets = {
			VkOffset3D {},
			VkOffset3D {.x = static_cast<int>(dst.width), .y = static_cast<int>(dst.height), .z = 1}
		},
	};

	vkCmdBlitImage(
		command_buffer,
		src.handle,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dst.handle,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&image_blit,
		VK_FILTER_NEAREST
	);
}

 

