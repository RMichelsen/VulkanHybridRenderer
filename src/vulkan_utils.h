#pragma once

namespace VkUtils {
inline void InsertImageBarrier(VkCommandBuffer command_buffer, VkImage image,
	VkImageAspectFlags aspect_flags, VkImageLayout old_layout, VkImageLayout new_layout,
	VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
	VkAccessFlags src_access, VkAccessFlags dst_access) {
	VkImageMemoryBarrier image_memory_barrier {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = src_access,
		.dstAccessMask = dst_access,
		.oldLayout = old_layout,
		.newLayout = new_layout,
		.image = image,
		.subresourceRange = VkImageSubresourceRange {
			.aspectMask = aspect_flags,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, nullptr,
		0, nullptr, 1, &image_memory_barrier);
}

template<typename T>
inline void ExecuteOneTimeCommands(VkDevice device, VkQueue queue,
	VkCommandPool command_pool, T commands) {
	VkCommandBufferAllocateInfo cmdbuf_alloc_info {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	VkCommandBuffer command_buffer;
	VK_CHECK(vkAllocateCommandBuffers(device, &cmdbuf_alloc_info, &command_buffer));

	VkCommandBufferBeginInfo cmdbuf_begin_info {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	VK_CHECK(vkBeginCommandBuffer(command_buffer, &cmdbuf_begin_info));

	// Run commands given by lambda
	commands(command_buffer);
	VK_CHECK(vkEndCommandBuffer(command_buffer));	

	VkFenceCreateInfo fence_info {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	};
	VkFence fence;
	VK_CHECK(vkCreateFence(device, &fence_info, nullptr, &fence));

	VkSubmitInfo submit_info {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &command_buffer
	}; 
	VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, fence));
	VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));

	vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
	vkDestroyFence(device, fence, nullptr);
}

inline bool IsDepthFormat(VkFormat format) {
	switch(format) {
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return true;
	default:
		return false;
	}
}

inline VkImageCreateInfo ImageCreateInfo2D(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage) {
	return VkImageCreateInfo {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = VkExtent3D {
			.width = width,
			.height = height,
			.depth = 1
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};
}

inline VkImageViewCreateInfo ImageViewCreateInfo2D(VkImage image, VkFormat format) {
	VkImageAspectFlags aspect_mask = VkUtils::IsDepthFormat(format) ?
		VK_IMAGE_ASPECT_DEPTH_BIT :
		VK_IMAGE_ASPECT_COLOR_BIT;
	return VkImageViewCreateInfo {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.components = VkComponentMapping {
			.r = VK_COMPONENT_SWIZZLE_R,
			.g = VK_COMPONENT_SWIZZLE_G,
			.b = VK_COMPONENT_SWIZZLE_B,
			.a = VK_COMPONENT_SWIZZLE_A
		},
		.subresourceRange = VkImageSubresourceRange {
			.aspectMask = aspect_mask,
			.levelCount = 1,
			.layerCount = 1
		}
	};
}

inline VkBufferImageCopy BufferImageCopy2D(uint32_t width, uint32_t height) {
	return VkBufferImageCopy {
		.imageSubresource = VkImageSubresourceLayers {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.layerCount = 1
		},
		.imageExtent = VkExtent3D {
			.width = width,
			.height = height,
			.depth = 1
		}
	};
}

inline VkBufferCreateInfo BufferCreateInfo(VkDeviceSize size, VkBufferUsageFlags usage) {
	return VkBufferCreateInfo {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage
	};
}

inline MappedBuffer CreateMappedBuffer(VmaAllocator allocator, VkBufferCreateInfo buffer_info) {
	MappedBuffer buffer;
	VmaAllocationCreateInfo buffer_alloc_info {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_CPU_TO_GPU
	};
	vmaCreateBuffer(allocator, &buffer_info, &buffer_alloc_info,
		&buffer.handle, &buffer.allocation, nullptr);
	vmaMapMemory(allocator, buffer.allocation, reinterpret_cast<void **>(&buffer.mapped_data));
	return buffer;
}

inline GPUBuffer CreateGPUBuffer(VmaAllocator allocator, VkBufferCreateInfo buffer_info) {
	GPUBuffer buffer;
	VmaAllocationCreateInfo buffer_alloc_info {
		.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.usage = VMA_MEMORY_USAGE_GPU_ONLY
	};
	vmaCreateBuffer(allocator, &buffer_info, &buffer_alloc_info,
		&buffer.handle, &buffer.allocation, nullptr);
	return buffer;
}

inline void DestroyMappedBuffer(VmaAllocator allocator, MappedBuffer buffer) {
	vmaUnmapMemory(allocator, buffer.allocation);
	vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
}

inline void DestroyGPUBuffer(VmaAllocator allocator, GPUBuffer buffer) {
	vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
}
}