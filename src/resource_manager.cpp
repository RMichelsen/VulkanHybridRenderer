#include "pch.h"
#include "resource_manager.h"
#include "vulkan_context.h"
#include "vulkan_utils.h"

#define MAX_TEXTURES 1024

ResourceManager::ResourceManager(VulkanContext &context) : context(context) {
	VkBufferCreateInfo buffer_info {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = 1024 * 1024 * 128,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
	};
	VmaAllocationCreateInfo buffer_alloc_info {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_CPU_TO_GPU
	};
	vmaCreateBuffer(context.allocator, &buffer_info, &buffer_alloc_info,
		&global_vertex_buffer.handle, &global_vertex_buffer.allocation, nullptr);

	buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	vmaCreateBuffer(context.allocator, &buffer_info, &buffer_alloc_info,
		&global_index_buffer.handle, &global_index_buffer.allocation, nullptr);

	vmaMapMemory(context.allocator, global_vertex_buffer.allocation, 
		reinterpret_cast<void **>(&global_vertex_buffer.mapped_data));
	vmaMapMemory(context.allocator, global_index_buffer.allocation, 
		reinterpret_cast<void **>(&global_index_buffer.mapped_data));

	VkDescriptorPoolSize descriptor_pool_size {
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = MAX_TEXTURES
	};
	VkDescriptorPoolCreateInfo descriptor_pool_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &descriptor_pool_size
	};
	VK_CHECK(vkCreateDescriptorPool(context.device, &descriptor_pool_info, nullptr, 
		&descriptor_pool));

	VkDescriptorSetLayoutBinding descriptor_set_layout_binding {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = MAX_TEXTURES,
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
	VK_CHECK(vkCreateDescriptorSetLayout(context.device, &descriptor_set_layout_info, nullptr,
		&descriptor_set_layout));


	uint32_t alloc_count = MAX_TEXTURES;
	VkDescriptorSetVariableDescriptorCountAllocateInfo descriptor_set_variable_alloc_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT,
		.descriptorSetCount = 1,
		.pDescriptorCounts = &alloc_count
	};
	VkDescriptorSetAllocateInfo descriptor_set_alloc_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = &descriptor_set_variable_alloc_info,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &descriptor_set_layout
	};
	VK_CHECK(vkAllocateDescriptorSets(context.device, &descriptor_set_alloc_info, &descriptor_set));

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
	vmaUnmapMemory(context.allocator, global_vertex_buffer.allocation);
	vmaUnmapMemory(context.allocator, global_index_buffer.allocation);
	vmaDestroyBuffer(context.allocator, global_vertex_buffer.handle, 
		global_vertex_buffer.allocation);
	vmaDestroyBuffer(context.allocator, global_index_buffer.handle, 
		global_index_buffer.allocation);
}

int ResourceManager::CreateTexture(uint32_t x, uint32_t y, uint8_t *data) {
	VkImageCreateInfo image_info {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = VkExtent3D {
			.width = x,
			.height = y,
			.depth = 1
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_LINEAR,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED	
	};
	VmaAllocation image_allocation;
	VmaAllocationCreateInfo image_alloc_info {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY
	};
	VkImage image;
	vmaCreateImage(context.allocator, &image_info, &image_alloc_info,
		&image, &image_allocation, nullptr);

	VkBufferCreateInfo buffer_info {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = x * y * 4, // Assuming RGBA, 8-bit each
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	};
	VmaAllocation buffer_allocation;
	VmaAllocationCreateInfo buffer_alloc_info {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_CPU_TO_GPU
	};
	VkBuffer buffer;
	vmaCreateBuffer(context.allocator, &buffer_info, &buffer_alloc_info,
		&buffer, &buffer_allocation, nullptr);

	void *mapped_data;
	vmaMapMemory(context.allocator, buffer_allocation, &mapped_data);
	memcpy(mapped_data, data, x * y * 4);

	VkUtils::ExecuteOneTimeCommands(context.device, context.graphics_queue, 
		context.command_pool, [&](VkCommandBuffer command_buffer) {

		VkUtils::InsertImageBarrier(command_buffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, VK_ACCESS_TRANSFER_WRITE_BIT);	

		VkBufferImageCopy buffer_image_copy {
			.imageSubresource = VkImageSubresourceLayers {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.layerCount = 1
			},
			.imageExtent = VkExtent3D {
				.width = x,
				.height = y,
				.depth = 1
			}
		};
		vkCmdCopyBufferToImage(command_buffer, buffer, image, 
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer_image_copy);

		VkUtils::InsertImageBarrier(command_buffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);	
	});

	vmaUnmapMemory(context.allocator, buffer_allocation);
	vmaDestroyBuffer(context.allocator, buffer, buffer_allocation);

	// TODO: Make helper for image view creation
	VkImageView image_view;
	VkImageViewCreateInfo image_view_info {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.components = VkComponentMapping {
			.r = VK_COMPONENT_SWIZZLE_R,	
			.g = VK_COMPONENT_SWIZZLE_G,	
			.b = VK_COMPONENT_SWIZZLE_B,	
			.a = VK_COMPONENT_SWIZZLE_A
		},
		.subresourceRange = VkImageSubresourceRange {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.layerCount = 1
		}
	};
	VK_CHECK(vkCreateImageView(context.device, &image_view_info, nullptr, &image_view));

	textures.push_back(Texture {
		.image = image,
		.image_view = image_view,
		.allocation = image_allocation
	});

	return static_cast<int>(textures.size()) - 1;
}

void ResourceManager::UpdateDescriptors() {
	std::vector<VkDescriptorImageInfo> descriptors;
	for(int i = 0; i < textures.size(); ++i) {
		descriptors.push_back(VkDescriptorImageInfo {
			.sampler = sampler,
			.imageView = textures[i].image_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		});
	}

	VkWriteDescriptorSet write_descriptor_set {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptor_set,
		.dstBinding = 0,
		.descriptorCount = static_cast<uint32_t>(descriptors.size()),
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = descriptors.data()
	};

	vkUpdateDescriptorSets(context.device, 1, &write_descriptor_set, 0, nullptr);
}
