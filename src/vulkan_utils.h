#pragma once

namespace VkUtils {
inline uint32_t AlignUp(uint32_t value, uint32_t alignment) {
	return (value + (alignment - 1)) & ~(alignment - 1);
}

inline std::vector<uint32_t> LoadShader(const char *path, VkShaderStageFlags shader_stage) {
	HANDLE file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	assert(file != INVALID_HANDLE_VALUE);

	DWORD file_size = GetFileSize(file, NULL);
	assert(file_size != INVALID_FILE_SIZE);

	std::vector<uint32_t> bytecode;
	bytecode.resize(file_size / sizeof(uint32_t));

	DWORD bytes_read;
	BOOL success = ReadFile(file, bytecode.data(), file_size, &bytes_read, NULL);
	assert(success);

	CloseHandle(file);
	return bytecode;
}

inline VkShaderModule CreateShaderModule(VkDevice device, const char *path, 
	VkShaderStageFlags shader_stage) {
	std::vector<uint32_t> shader_bytecode = VkUtils::LoadShader(path, shader_stage);
	VkShaderModule shader;
	VkShaderModuleCreateInfo shader_module_info {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = shader_bytecode.size() * sizeof(uint32_t),
		.pCode = shader_bytecode.data()
	};
	VK_CHECK(vkCreateShaderModule(device, &shader_module_info, nullptr, &shader));
	return shader;
}

inline VkPipelineShaderStageCreateInfo PipelineShaderStageCreateInfo(VkDevice device, const char *path,
	VkShaderStageFlagBits shader_stage) {
	return VkPipelineShaderStageCreateInfo {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = shader_stage,
		.module = VkUtils::CreateShaderModule(device, path, shader_stage),
		.pName = "main"
	};
}

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

inline VkDeviceOrHostAddressConstKHR GetDeviceAddressConst(VkDevice device, VkBuffer buffer) {
	VkBufferDeviceAddressInfoKHR buffer_device_address_info {
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR,
		.buffer = buffer
	};
	return VkDeviceOrHostAddressConstKHR {
		.deviceAddress = vkGetBufferDeviceAddressKHR(device, &buffer_device_address_info)
	};
}

inline VkDeviceOrHostAddressKHR GetDeviceAddress(VkDevice device, VkBuffer buffer) {
	VkBufferDeviceAddressInfoKHR buffer_device_address_info {
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR,
		.buffer = buffer
	};
	return VkDeviceOrHostAddressKHR {
		.deviceAddress = vkGetBufferDeviceAddressKHR(device, &buffer_device_address_info)
	};
}

inline VkDeviceOrHostAddressKHR GetAccelerationStructureAddress(VkDevice device, VkAccelerationStructureKHR acceleration_structure) {
	VkAccelerationStructureDeviceAddressInfoKHR acceleration_structure_address_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = acceleration_structure
	};
	return VkDeviceOrHostAddressKHR {
		.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &acceleration_structure_address_info)
	};
}

inline void DestroyMappedBuffer(VmaAllocator allocator, MappedBuffer buffer) {
	vmaUnmapMemory(allocator, buffer.allocation);
	vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
}

inline void DestroyGPUBuffer(VmaAllocator allocator, GPUBuffer buffer) {
	vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
}

inline void DestroyImage(VkDevice device, VmaAllocator allocator, Image image) {
	vkDestroyImageView(device, image.view, nullptr);
	vmaDestroyImage(allocator, image.handle, image.allocation);
}

inline void DestroyAccelerationStructure(VkDevice device, VmaAllocator allocator,
	AccelerationStructure acceleration_structure) {
	VkUtils::DestroyGPUBuffer(allocator, acceleration_structure.buffer);
	VkUtils::DestroyGPUBuffer(allocator, acceleration_structure.scratch);
	vkDestroyAccelerationStructureKHR(device, acceleration_structure.handle, nullptr);
}

inline VkImageLayout GetImageLayoutFromResourceType(TransientImageType type, VkFormat format) {
	switch(type) {
	case TransientImageType::AttachmentImage: {
		return VkUtils::IsDepthFormat(format) ? 
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	case TransientImageType::SampledImage: {
		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}
	case TransientImageType::StorageImage: {
		return VK_IMAGE_LAYOUT_GENERAL;
	}
	}

	return VK_IMAGE_LAYOUT_UNDEFINED;
}

inline VkImageUsageFlags GetImageUsageFromResourceType(TransientImageType type, VkFormat format) {
	switch(type) {
	case TransientImageType::AttachmentImage: {
		return VkUtils::IsDepthFormat(format) ?
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	} break;
	case TransientImageType::SampledImage: {
		return VK_IMAGE_USAGE_SAMPLED_BIT;
	} break;
	case TransientImageType::StorageImage: {
		return VK_IMAGE_USAGE_STORAGE_BIT;
	} break;
	}

	return 0;
}

inline VkDescriptorImageInfo DescriptorImageInfo(VkImageView image_view, VkImageLayout layout,
	VkSampler sampler = VK_NULL_HANDLE) {
	return VkDescriptorImageInfo {
		.sampler = sampler,
		.imageView = image_view,
		.imageLayout = layout
	};
}

inline VkDescriptorSetLayoutBinding DescriptorSetLayoutBinding(uint32_t binding, VkDescriptorType type,
	VkShaderStageFlags shader_stage, uint32_t count = 1) {
	return VkDescriptorSetLayoutBinding {
		.binding = binding,
		.descriptorType = type,
		.descriptorCount = count,
		.stageFlags = shader_stage
	};
}

inline TransientResource CreateTransientBackbuffer(uint32_t binding, ColorBlendState color_blend_state) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = "BACKBUFFER",
		.image = TransientImage {
			.type = TransientImageType::AttachmentImage,
			.width = 0,
			.height = 0,
			.format = VK_FORMAT_UNDEFINED,
			.binding = binding,
			.color_blend_state = color_blend_state
		}
	};
}

inline TransientResource CreateTransientAttachmentImage(const char *name, VkFormat format, uint32_t binding,
	ColorBlendState color_blend_state = ColorBlendState::Off) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = name,
		.image = TransientImage {
			.type = TransientImageType::AttachmentImage,
			.width = 0,
			.height = 0,
			.format = format,
			.binding = binding,
			.color_blend_state = color_blend_state
		}
	};
}

inline TransientResource CreateTransientAttachmentImage(const char *name, uint32_t width, uint32_t height,
	VkFormat format, uint32_t binding, ColorBlendState color_blend_state = ColorBlendState::Off) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = name,
		.image = TransientImage {
			.type = TransientImageType::AttachmentImage,
			.width = width,
			.height = height,
			.format = format,
			.binding = binding,
			.color_blend_state = color_blend_state
		}
	};
}

inline TransientResource CreateTransientSampledImage(const char *name, VkFormat format,
	uint32_t binding) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = name,
		.image = TransientImage {
			.type = TransientImageType::SampledImage,
			.width = 0,
			.height = 0,
			.format = format,
			.binding = binding
		}
	};
}

inline TransientResource CreateTransientSampledImage(const char *name, uint32_t width,
	uint32_t height, VkFormat format, uint32_t binding) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = name,
		.image = TransientImage {
			.type = TransientImageType::SampledImage,
			.width = width,
			.height = height,
			.format = format,
			.binding = binding
		}
	};
}

inline TransientResource CreateTransientStorageImage(const char *name, VkFormat format,
	uint32_t binding) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = name,
		.image = TransientImage {
			.type = TransientImageType::StorageImage,
			.width = 0,
			.height = 0,
			.format = format,
			.binding = binding
		}
	};
}

inline TransientResource CreateTransientStorageImage(const char *name, uint32_t width,
	uint32_t height, VkFormat format, uint32_t binding) {
	return TransientResource {
		.type = TransientResourceType::Image,
		.name = name,
		.image = TransientImage {
			.type = TransientImageType::StorageImage,
			.width = width,
			.height = height,
			.format = format,
			.binding = binding
		}
	};
}
}

