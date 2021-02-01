#include "pch.h"
#include "resource_manager.h"

#include "vulkan_context.h"
#include "vulkan_utils.h"

#define MAX_TRANSIENT_TEXTURES 256
#define MAX_TRANSIENT_SETS 128

#define MAX_GLOBAL_TEXTURES 1024
#define MAX_PER_FRAME_UBOS MAX_FRAMES_IN_FLIGHT

ResourceManager::ResourceManager(VulkanContext &context) : context(context) {
	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(1024 * 1024 * 128, 
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	global_vertex_buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);
	buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	global_index_buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);

	VkDescriptorPoolSize transient_descriptor_pool_size {
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = MAX_TRANSIENT_TEXTURES
	};
	VkDescriptorPoolCreateInfo transient_descriptor_pool_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = MAX_TRANSIENT_SETS,
		.poolSizeCount = 1,
		.pPoolSizes = &transient_descriptor_pool_size
	};
	VK_CHECK(vkCreateDescriptorPool(context.device, &transient_descriptor_pool_info, 
		nullptr, &transient_descriptor_pool));

	CreateGlobalDescriptorSet();
	CreatePerFrameDescriptorSet();
	CreatePerFrameUBOs();

	VkSamplerCreateInfo sampler_info {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.anisotropyEnable = VK_TRUE,
		.maxAnisotropy = context.gpu.properties.properties.limits.maxSamplerAnisotropy,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK
	};
	VK_CHECK(vkCreateSampler(context.device, &sampler_info, nullptr, &sampler));
}

ResourceManager::~ResourceManager() {
	VkUtils::DestroyGPUBuffer(context.allocator, global_vertex_buffer);
	VkUtils::DestroyGPUBuffer(context.allocator, global_index_buffer);
}

Texture ResourceManager::CreateTransientTexture(uint32_t width, uint32_t height, VkFormat format) {
	Texture texture;

	VkImageUsageFlags usage = VkUtils::IsDepthFormat(format) ?
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT :
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	VkImageCreateInfo image_info = VkUtils::ImageCreateInfo2D(width, height, format, usage);

	VmaAllocationCreateInfo image_alloc_info {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY
	};
	vmaCreateImage(context.allocator, &image_info, &image_alloc_info,
		&texture.image, &texture.allocation, nullptr);

	VkImageViewCreateInfo image_view_info = VkUtils::ImageViewCreateInfo2D(texture.image, format);
	VK_CHECK(vkCreateImageView(context.device, &image_view_info, nullptr, &texture.image_view));

	return texture;
}

int ResourceManager::LoadTextureFromData(uint32_t width, uint32_t height, uint8_t *data) {
	Texture texture;

	VkImageCreateInfo image_info = VkUtils::ImageCreateInfo2D(width, height, 
		VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	VmaAllocationCreateInfo image_alloc_info {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY
	};
	vmaCreateImage(context.allocator, &image_info, &image_alloc_info,
		&texture.image, &texture.allocation, nullptr);

	// TODO: Assuming RGBA, 8-bit each
	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(width * height * 4, 
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

	MappedBuffer buffer = VkUtils::CreateMappedBuffer(context.allocator, buffer_info);
	memcpy(buffer.mapped_data, data, width * height * 4);

	VkUtils::ExecuteOneTimeCommands(context.device, context.graphics_queue, 
		context.command_pool, [&](VkCommandBuffer command_buffer) {

		VkUtils::InsertImageBarrier(command_buffer, texture.image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, VK_ACCESS_TRANSFER_WRITE_BIT);	

		VkBufferImageCopy buffer_image_copy = VkUtils::BufferImageCopy2D(width, height);
		vkCmdCopyBufferToImage(command_buffer, buffer.handle, texture.image, 
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer_image_copy);

		VkUtils::InsertImageBarrier(command_buffer, texture.image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);	
	});

	VkUtils::DestroyMappedBuffer(context.allocator, buffer);

	// TODO: Make helper for image view creation
	VkImageViewCreateInfo image_view_info = VkUtils::ImageViewCreateInfo2D(texture.image, 
		VK_FORMAT_R8G8B8A8_UNORM);
	VK_CHECK(vkCreateImageView(context.device, &image_view_info, nullptr, &texture.image_view));

	textures.push_back(texture);
	return static_cast<int>(textures.size()) - 1;
}

void ResourceManager::UpdateDescriptors() {
	std::vector<VkDescriptorImageInfo> descriptors;
	for(Texture &texture : textures) {
		descriptors.push_back(VkDescriptorImageInfo {
			.sampler = sampler,
			.imageView = texture.image_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		});
	}

	VkWriteDescriptorSet write_descriptor_set {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = global_descriptor_set,
		.dstBinding = 0,
		.descriptorCount = static_cast<uint32_t>(descriptors.size()),
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = descriptors.data()
	};

	vkUpdateDescriptorSets(context.device, 1, &write_descriptor_set, 0, nullptr);
}

void ResourceManager::UploadDataToGPUBuffer(GPUBuffer buffer, void *data, VkDeviceSize size) {
	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(size, 
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	MappedBuffer staging_buffer = VkUtils::CreateMappedBuffer(context.allocator, buffer_info);
	memcpy(staging_buffer.mapped_data, data, size);

	VkUtils::ExecuteOneTimeCommands(context.device, context.graphics_queue, context.command_pool,
		[=](VkCommandBuffer command_buffer) {
			VkBufferCopy buffer_copy {
				.size = size
			};
			vkCmdCopyBuffer(command_buffer, staging_buffer.handle, buffer.handle, 1, &buffer_copy);
		}
	);

	VkUtils::DestroyMappedBuffer(context.allocator, staging_buffer);
}

void ResourceManager::UpdatePerFrameUBO(uint32_t resource_idx, PerFrameData per_frame_data) {
	memcpy(per_frame_ubos[resource_idx].mapped_data, &per_frame_data, sizeof(PerFrameData));
}

void ResourceManager::CreateGlobalDescriptorSet() {
	VkDescriptorPoolSize descriptor_pool_size {
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = MAX_GLOBAL_TEXTURES
	};
	VkDescriptorPoolCreateInfo descriptor_pool_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &descriptor_pool_size
	};
	VK_CHECK(vkCreateDescriptorPool(context.device, &descriptor_pool_info, nullptr, 
		&global_descriptor_pool));

	VkDescriptorSetLayoutBinding descriptor_set_layout_binding {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = MAX_GLOBAL_TEXTURES,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
	};
	std::array<VkDescriptorBindingFlags, 1> descriptor_binding_flags {
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
		VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
	};
	VkDescriptorSetLayoutBindingFlagsCreateInfo descriptor_set_layout_binding_flags_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = static_cast<uint32_t>(descriptor_binding_flags.size()),
		.pBindingFlags = descriptor_binding_flags.data()
	};
	VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &descriptor_set_layout_binding_flags_info,
		.bindingCount = 1,
		.pBindings = &descriptor_set_layout_binding
	};
	VK_CHECK(vkCreateDescriptorSetLayout(context.device, &descriptor_set_layout_info, 
		nullptr, &global_descriptor_set_layout));

	uint32_t alloc_count = MAX_GLOBAL_TEXTURES;
	VkDescriptorSetVariableDescriptorCountAllocateInfo descriptor_set_variable_alloc_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT,
		.descriptorSetCount = 1,
		.pDescriptorCounts = &alloc_count
	};
	VkDescriptorSetAllocateInfo descriptor_set_alloc_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = &descriptor_set_variable_alloc_info,
		.descriptorPool = global_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &global_descriptor_set_layout
	};
	VK_CHECK(vkAllocateDescriptorSets(context.device, &descriptor_set_alloc_info, 
		&global_descriptor_set));
}

void ResourceManager::CreatePerFrameDescriptorSet() {
	VkDescriptorPoolSize descriptor_pool_size {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = MAX_PER_FRAME_UBOS
	};
	VkDescriptorPoolCreateInfo descriptor_pool_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &descriptor_pool_size
	};
	VK_CHECK(vkCreateDescriptorPool(context.device, &descriptor_pool_info, nullptr,
		&per_frame_descriptor_pool));

	VkDescriptorSetLayoutBinding descriptor_set_layout_binding {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = MAX_PER_FRAME_UBOS,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
	};
	VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &descriptor_set_layout_binding
	};
	VK_CHECK(vkCreateDescriptorSetLayout(context.device, &descriptor_set_layout_info,
		nullptr, &per_frame_descriptor_set_layout));
	VkDescriptorSetAllocateInfo descriptor_set_alloc_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = per_frame_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &per_frame_descriptor_set_layout
	};
	VK_CHECK(vkAllocateDescriptorSets(context.device, &descriptor_set_alloc_info,
		&per_frame_descriptor_set));

	VkPipelineLayoutCreateInfo pipeline_layout_info {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &per_frame_descriptor_set_layout
	};
	VK_CHECK(vkCreatePipelineLayout(context.device, &pipeline_layout_info,
		nullptr, &global_pipeline_layout));
}

void ResourceManager::CreatePerFrameUBOs() {
	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(sizeof(PerFrameData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	std::vector<VkDescriptorBufferInfo> descriptors;
	for(MappedBuffer &buffer : per_frame_ubos) {
		buffer = VkUtils::CreateMappedBuffer(context.allocator, buffer_info);

		descriptors.push_back(VkDescriptorBufferInfo {
			.buffer = buffer.handle,
			.offset = 0,
			.range = sizeof(PerFrameData)
		});
	}

	VkWriteDescriptorSet write_descriptor_set {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = per_frame_descriptor_set,
		.dstBinding = 0,
		.descriptorCount = static_cast<uint32_t>(descriptors.size()),
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = descriptors.data()
	};

	vkUpdateDescriptorSets(context.device, 1, &write_descriptor_set, 0, nullptr);
}
