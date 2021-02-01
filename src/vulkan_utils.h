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
}