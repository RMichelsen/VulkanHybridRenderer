#include "pch.h"
#include "resource_manager.h"

#include "rendering_backend/vulkan_context.h"
#include "rendering_backend/vulkan_utils.h"
#include "scene/scene_loader.h"

inline constexpr uint32_t MAX_TRANSIENT_DESCRIPTORS_PER_TYPE = 256;
inline constexpr uint32_t MAX_TRANSIENT_SETS = 128;

inline constexpr uint32_t MAX_PER_FRAME_UBOS = MAX_FRAMES_IN_FLIGHT;

inline constexpr uint32_t MAX_VERTEX_AND_INDEX_BUFSIZE = 128 * 1024 * 1024; // 128MB

ResourceManager::ResourceManager(VulkanContext &context) : context(context) {
	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(MAX_VERTEX_AND_INDEX_BUFSIZE, 
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | 
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	global_vertex_buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);

	buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	global_index_buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);

	buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	global_obj_data_buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);

	std::array<VkDescriptorPoolSize, 3> transient_descriptor_pool_sizes {
		VkDescriptorPoolSize {
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_TRANSIENT_DESCRIPTORS_PER_TYPE
		},
		VkDescriptorPoolSize {
			.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = MAX_TRANSIENT_DESCRIPTORS_PER_TYPE
		},
		VkDescriptorPoolSize {
			.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = MAX_TRANSIENT_DESCRIPTORS_PER_TYPE
		}
	};
	VkDescriptorPoolCreateInfo transient_descriptor_pool_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = MAX_TRANSIENT_SETS,
		.poolSizeCount = static_cast<uint32_t>(transient_descriptor_pool_sizes.size()),
		.pPoolSizes = transient_descriptor_pool_sizes.data()
	};
	VK_CHECK(vkCreateDescriptorPool(context.device, &transient_descriptor_pool_info, 
		nullptr, &transient_descriptor_pool));

	CreateGlobalDescriptorSet();
	CreatePerFrameDescriptorSet();
	CreatePerFrameUBOs();

	VkSamplerCreateInfo sampler_info {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.anisotropyEnable = VK_TRUE,
		.maxAnisotropy = context.gpu.properties.properties.limits.maxSamplerAnisotropy,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK
	};
	VK_CHECK(vkCreateSampler(context.device, &sampler_info, nullptr, &default_sampler));
}

void ResourceManager::DestroyResources() {
	VK_CHECK(vkDeviceWaitIdle(context.device));

	VkUtils::DestroyGPUBuffer(context.allocator, global_vertex_buffer);
	VkUtils::DestroyGPUBuffer(context.allocator, global_index_buffer);
	VkUtils::DestroyGPUBuffer(context.allocator, global_obj_data_buffer);

	for(uint32_t i = 0; i < MAX_GLOBAL_TEXTURES; ++i) {
		if(textures[i].handle != VK_NULL_HANDLE) {
			VkUtils::DestroyImage(context.device, context.allocator, textures[i]);
		}
	}

	VkUtils::DestroyAccelerationStructure(context.device, context.allocator, global_BLAS);
	VkUtils::DestroyAccelerationStructure(context.device, context.allocator, global_TLAS);

	vkDestroyDescriptorPool(context.device, global_descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, global_descriptor_set_layout, nullptr);
	vkDestroyDescriptorPool(context.device, per_frame_descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, per_frame_descriptor_set_layout, nullptr);
	vkDestroyDescriptorPool(context.device, transient_descriptor_pool, nullptr);

	for(MappedBuffer &buffer : per_frame_ubos) {
		VkUtils::DestroyMappedBuffer(context.allocator, buffer);
	}

	for(Sampler &sampler : samplers) {
		vkDestroySampler(context.device, sampler.handle, nullptr);
	}
	vkDestroySampler(context.device, default_sampler, nullptr);
}

void ResourceManager::LoadScene(const char* scene_path) {
	std::string full_scene_path = "data/models/" + std::string(scene_path);
	scene = SceneLoader::LoadScene(*this, full_scene_path.c_str());
}

Image ResourceManager::Create2DImage(uint32_t width, uint32_t height, VkFormat format, 
	VkImageUsageFlags usage, VkImageLayout initial_layout) {
	Image image {
		.width = width,
		.height = height,
		.format = format,
		.usage = usage
	};
	VkImageCreateInfo image_info = VkUtils::ImageCreateInfo2D(width, height, format, usage);

	VmaAllocationCreateInfo image_alloc_info {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY
	};
	vmaCreateImage(context.allocator, &image_info, &image_alloc_info,
		&image.handle, &image.allocation, nullptr);

	VkImageViewCreateInfo image_view_info = VkUtils::ImageViewCreateInfo2D(image.handle, format);
	VK_CHECK(vkCreateImageView(context.device, &image_view_info, nullptr, &image.view));

	VkImageAspectFlags aspect_flags = VkUtils::IsDepthFormat(format) ?
		VK_IMAGE_ASPECT_DEPTH_BIT :
		VK_IMAGE_ASPECT_COLOR_BIT;

	if(initial_layout != VK_IMAGE_LAYOUT_UNDEFINED) {
		VkUtils::ExecuteOneTimeCommands(context.device, context.graphics_queue, context.command_pool,
			[&](VkCommandBuffer command_buffer) {
				VkUtils::InsertImageBarrier(command_buffer, image.handle,
					aspect_flags, VK_IMAGE_LAYOUT_UNDEFINED, initial_layout,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);
			}
		);
	}

	return image;
}

uint32_t ResourceManager::UploadTextureFromData(uint32_t width, uint32_t height, uint8_t *data, 
	SamplerInfo *sampler_info) {
	Image texture;

	VkImageCreateInfo image_info = VkUtils::ImageCreateInfo2D(width, height, 
		VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	VmaAllocationCreateInfo image_alloc_info {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY
	};
	vmaCreateImage(context.allocator, &image_info, &image_alloc_info,
		&texture.handle, &texture.allocation, nullptr);

	// TODO: Assuming RGBA, 8-bit each
	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(static_cast<VkDeviceSize>(width) * 
		static_cast<VkDeviceSize>(height) * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

	MappedBuffer buffer = VkUtils::CreateMappedBuffer(context.allocator, buffer_info);
	memcpy(buffer.mapped_data, data, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

	VkUtils::ExecuteOneTimeCommands(context.device, context.graphics_queue, 
		context.command_pool, [&](VkCommandBuffer command_buffer) {

		VkUtils::InsertImageBarrier(command_buffer, texture.handle, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, VK_ACCESS_TRANSFER_WRITE_BIT);	

		VkBufferImageCopy buffer_image_copy = VkUtils::BufferImageCopy2D(width, height);
		vkCmdCopyBufferToImage(command_buffer, buffer.handle, texture.handle, 
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer_image_copy);

		VkUtils::InsertImageBarrier(command_buffer, texture.handle, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);	
	});

	VkUtils::DestroyMappedBuffer(context.allocator, buffer);

	VkImageViewCreateInfo image_view_info = VkUtils::ImageViewCreateInfo2D(texture.handle, 
		VK_FORMAT_R8G8B8A8_UNORM);
	VK_CHECK(vkCreateImageView(context.device, &image_view_info, nullptr, &texture.view));
	
	VkSampler texture_sampler = sampler_info ?
		VK_NULL_HANDLE :
		default_sampler;

	if(texture_sampler == VK_NULL_HANDLE) {
		for(Sampler &sampler : samplers) {
			if(sampler.info.mag_filter == sampler_info->mag_filter &&
				sampler.info.min_filter == sampler_info->min_filter &&
				sampler.info.address_mode_u == sampler_info->address_mode_u &&
				sampler.info.address_mode_v == sampler_info->address_mode_v) {
				texture_sampler = sampler.handle;
			}
		}

		if(texture_sampler == VK_NULL_HANDLE) {
			VkSamplerCreateInfo vk_sampler_info {
				.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
				.magFilter = sampler_info->mag_filter,
				.minFilter = sampler_info->min_filter,
				.addressModeU = sampler_info->address_mode_u,
				.addressModeV = sampler_info->address_mode_v,
				.addressModeW = sampler_info->address_mode_v,
				.anisotropyEnable = VK_TRUE,
				.maxAnisotropy = context.gpu.properties.properties.limits.maxSamplerAnisotropy,
				.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK
			};
			vkCreateSampler(context.device, &vk_sampler_info, nullptr, &texture_sampler);
			samplers.emplace_back(Sampler {
				.handle = texture_sampler,
				.info = *sampler_info
			});
		}
	}

	// TODO: OPTIMIZE
	for(uint32_t i = 0; i < MAX_GLOBAL_TEXTURES; ++i) {
		if(textures[i].handle == VK_NULL_HANDLE) {
			textures[i] = texture;

			VkDescriptorImageInfo descriptor {
				.sampler = texture_sampler,
				.imageView = texture.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			};

			VkWriteDescriptorSet write_descriptor_set {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = global_descriptor_set,
				.dstBinding = 4,
				.dstArrayElement = i,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &descriptor
			};

			vkUpdateDescriptorSets(context.device, 1, &write_descriptor_set, 0, nullptr);
			return i;
		}
	}

	assert(false && "No free texture slots left!");
	return static_cast<uint32_t>(-1);
}

void ResourceManager::TagImage(Image &image, const char *name) {
	VkDebugUtilsObjectNameInfoEXT debug_utils_object_name_info {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		.objectType = VK_OBJECT_TYPE_IMAGE,
		.objectHandle = reinterpret_cast<uint64_t>(image.handle),
		.pObjectName = name
	};
	vkSetDebugUtilsObjectNameEXT(context.device, &debug_utils_object_name_info);
}

void ResourceManager::TagImage(uint32_t image_idx, const char *name) {
	VkDebugUtilsObjectNameInfoEXT debug_utils_object_name_info {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		.objectType = VK_OBJECT_TYPE_IMAGE,
		.objectHandle = reinterpret_cast<uint64_t>(textures[image_idx].handle),
		.pObjectName = name
	};
	vkSetDebugUtilsObjectNameEXT(context.device, &debug_utils_object_name_info);
}

void ResourceManager::UpdateGeometry(std::vector<Vertex> &vertices, std::vector<uint32_t> &indices, Scene &scene) {
	UploadDataToGPUBuffer(global_vertex_buffer, vertices.data(), vertices.size() * sizeof(Vertex));
	UploadDataToGPUBuffer(global_index_buffer, indices.data(), indices.size() * sizeof(uint32_t));

	// TODO: OPTIMIZE
	std::vector<Primitive> primitives;
	for(Mesh &mesh : scene.meshes) {
		primitives.insert(primitives.end(), mesh.primitives.begin(), mesh.primitives.end());
	}

	UploadDataToGPUBuffer(global_obj_data_buffer, primitives.data(), primitives.size() * sizeof(Primitive));
	UpdateBLAS(static_cast<uint32_t>(vertices.size()), primitives);
	UpdateTLAS(primitives);

	VkDescriptorBufferInfo vertex_buffer_info {
		.buffer = global_vertex_buffer.handle,
		.range = VK_WHOLE_SIZE
	};
	VkDescriptorBufferInfo index_buffer_info {
		.buffer = global_index_buffer.handle,
		.range = VK_WHOLE_SIZE
	};
	VkDescriptorBufferInfo obj_data_buffer_info {
		.buffer = global_obj_data_buffer.handle,
		.range = VK_WHOLE_SIZE
	};
	VkWriteDescriptorSetAccelerationStructureKHR write_descriptor_set_acceleration_structure {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &global_TLAS.handle
	};

	std::vector<VkWriteDescriptorSet> write_descriptor_sets {
		VkWriteDescriptorSet {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = global_descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &vertex_buffer_info
		},
		VkWriteDescriptorSet {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = global_descriptor_set,
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &index_buffer_info
		},
		VkWriteDescriptorSet {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = global_descriptor_set,
			.dstBinding = 2,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &obj_data_buffer_info
		},
		VkWriteDescriptorSet {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = &write_descriptor_set_acceleration_structure,
			.dstSet = global_descriptor_set,
			.dstBinding = 3,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
		}
	};

	vkUpdateDescriptorSets(context.device, static_cast<uint32_t>(write_descriptor_sets.size()),
		write_descriptor_sets.data(), 0, nullptr);
}

void ResourceManager::UpdatePerFrameUBO(uint32_t resource_idx, PerFrameData per_frame_data) {
	memcpy(per_frame_ubos[resource_idx].mapped_data, &per_frame_data, sizeof(PerFrameData));
}

void ResourceManager::CreateGlobalDescriptorSet() {
	std::array<VkDescriptorPoolSize, 3> descriptor_pool_sizes {
		VkDescriptorPoolSize {
			.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 3
		},
		VkDescriptorPoolSize {
			.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
			.descriptorCount = 1
		},
		VkDescriptorPoolSize {
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_GLOBAL_TEXTURES
		}
	};
	VkDescriptorPoolCreateInfo descriptor_pool_info {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = static_cast<uint32_t>(descriptor_pool_sizes.size()),
		.pPoolSizes = descriptor_pool_sizes.data()
	};
	VK_CHECK(vkCreateDescriptorPool(context.device, &descriptor_pool_info, nullptr, 
		&global_descriptor_pool));

	std::array<VkDescriptorSetLayoutBinding, 5> descriptor_set_layout_bindings {
		VkDescriptorSetLayoutBinding {
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
		},
		VkDescriptorSetLayoutBinding {
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
		},
		VkDescriptorSetLayoutBinding {
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
		},
		VkDescriptorSetLayoutBinding {
			.binding = 3,
			.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_FRAGMENT_BIT
		},
		VkDescriptorSetLayoutBinding {
			.binding = 4,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_GLOBAL_TEXTURES,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
		}
	};
	std::array<VkDescriptorBindingFlags, 5> descriptor_binding_flags {
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
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
		.bindingCount = static_cast<uint32_t>(descriptor_set_layout_bindings.size()),
		.pBindings = descriptor_set_layout_bindings.data()
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
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | 
			VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
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
}

void ResourceManager::CreatePerFrameUBOs() {
	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(sizeof(PerFrameData), 
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
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

void ResourceManager::UpdateBLAS(uint32_t vertex_count, std::vector<Primitive> &primitives) {
	std::vector<VkAccelerationStructureGeometryKHR> acceleration_structure_geometries;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR> acceleration_structure_build_range_infos;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR *> acceleration_structure_build_range_pinfos;
	std::vector<VkTransformMatrixKHR> transform_data;
	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(
		sizeof(VkTransformMatrixKHR) * primitives.size(),
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	GPUBuffer transform_data_buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);
	std::vector<uint32_t> max_primitive_counts;

	acceleration_structure_build_range_infos.reserve(primitives.size());
	uint32_t transform_offset = 0;
	for(Primitive &primitive : primitives) {
		glm::vec4 row1 = glm::row(primitive.transform, 0);
		glm::vec4 row2 = glm::row(primitive.transform, 1);
		glm::vec4 row3 = glm::row(primitive.transform, 2);
		transform_data.emplace_back(VkTransformMatrixKHR {
			.matrix = { 
				row1[0], row1[1], row1[2], row1[3],
				row2[0], row2[1], row2[2], row2[3],
				row3[0], row3[1], row3[2], row3[3] 
			}
		});
		acceleration_structure_geometries.emplace_back(VkAccelerationStructureGeometryKHR {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
			.geometry = VkAccelerationStructureGeometryDataKHR {
				.triangles = VkAccelerationStructureGeometryTrianglesDataKHR {
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
					.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
					.vertexData = VkUtils::GetDeviceAddressConst(context.device, global_vertex_buffer.handle),
					.vertexStride = sizeof(Vertex),
					.maxVertex = primitive.vertex_offset + primitive.index_count / 3,
					.indexType = VK_INDEX_TYPE_UINT32,
					.indexData = VkUtils::GetDeviceAddressConst(context.device, global_index_buffer.handle),
					.transformData = VkUtils::GetDeviceAddressConst(context.device, transform_data_buffer.handle)
				}
			},
			.flags = VK_GEOMETRY_OPAQUE_BIT_KHR
		});

		acceleration_structure_build_range_infos.emplace_back(VkAccelerationStructureBuildRangeInfoKHR {
			.primitiveCount = primitive.index_count / 3,
			.primitiveOffset = primitive.index_offset * sizeof(uint32_t),
			.firstVertex = primitive.vertex_offset,
			.transformOffset = sizeof(VkTransformMatrixKHR) * transform_offset++
		});
		acceleration_structure_build_range_pinfos.emplace_back(&acceleration_structure_build_range_infos.back());
		max_primitive_counts.emplace_back(primitive.index_count / 3);
	}
	UploadDataToGPUBuffer(transform_data_buffer, transform_data.data(), transform_data.size() * sizeof(VkTransformMatrixKHR));
		
	VkAccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = static_cast<uint32_t>(acceleration_structure_geometries.size()),
		.pGeometries = acceleration_structure_geometries.data()
	};

	VkAccelerationStructureBuildSizesInfoKHR acceleration_structure_build_sizes_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
	};
	vkGetAccelerationStructureBuildSizesKHR(context.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&acceleration_structure_build_geometry_info,
		max_primitive_counts.data(),
		&acceleration_structure_build_sizes_info
	);

	buffer_info = VkUtils::BufferCreateInfo(
		acceleration_structure_build_sizes_info.accelerationStructureSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
	);
	global_BLAS.buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);
	buffer_info = VkUtils::BufferCreateInfo(
		acceleration_structure_build_sizes_info.buildScratchSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
	);
	global_BLAS.scratch = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);
	
	VkAccelerationStructureCreateInfoKHR acceleration_structure_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = global_BLAS.buffer.handle,
		.size = acceleration_structure_build_sizes_info.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
	};
	VK_CHECK(vkCreateAccelerationStructureKHR(context.device, &acceleration_structure_info, nullptr,
		&global_BLAS.handle));

	acceleration_structure_build_geometry_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	acceleration_structure_build_geometry_info.dstAccelerationStructure = global_BLAS.handle;
	acceleration_structure_build_geometry_info.scratchData = VkUtils::GetDeviceAddress(context.device, 
		global_BLAS.scratch.handle);

	VkUtils::ExecuteOneTimeCommands(context.device, context.graphics_queue, context.command_pool,
		[&](VkCommandBuffer command_buffer) {
			vkCmdBuildAccelerationStructuresKHR(command_buffer,
				1,
				&acceleration_structure_build_geometry_info,
				acceleration_structure_build_range_pinfos.data()
			);
		}
	);

	VkUtils::DestroyGPUBuffer(context.allocator, transform_data_buffer);
}

void ResourceManager::UpdateTLAS(std::vector<Primitive> &primitives) {
	VkAccelerationStructureInstanceKHR acceleration_structure_instance {
		.transform = VkTransformMatrixKHR {
			.matrix = {
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f
			}
		},
		.instanceCustomIndex = 0,
		.mask = 0xFF,
		.instanceShaderBindingTableRecordOffset = 0,
		.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
		.accelerationStructureReference = VkUtils::GetAccelerationStructureAddress(
			context.device, global_BLAS.handle).deviceAddress
	};

	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(sizeof(VkAccelerationStructureInstanceKHR),
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	GPUBuffer instances_buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);
	UploadDataToGPUBuffer(instances_buffer, &acceleration_structure_instance,
		sizeof(VkAccelerationStructureInstanceKHR));

	VkAccelerationStructureGeometryKHR acceleration_structure_geometry {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.geometry = VkAccelerationStructureGeometryDataKHR {
			.instances = VkAccelerationStructureGeometryInstancesDataKHR {
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
				.arrayOfPointers = VK_FALSE,
				.data = VkUtils::GetDeviceAddressConst(context.device, instances_buffer.handle)
			}
		},
		.flags = VK_GEOMETRY_OPAQUE_BIT_KHR
	};

	VkAccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &acceleration_structure_geometry
	};

	uint32_t max_primitive_counts = static_cast<uint32_t>(primitives.size());
	VkAccelerationStructureBuildSizesInfoKHR acceleration_structure_build_sizes_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
	};
	vkGetAccelerationStructureBuildSizesKHR(context.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &acceleration_structure_build_geometry_info,
		&max_primitive_counts, &acceleration_structure_build_sizes_info);

	buffer_info = VkUtils::BufferCreateInfo(
		acceleration_structure_build_sizes_info.accelerationStructureSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
	);
	global_TLAS.buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);
	buffer_info = VkUtils::BufferCreateInfo(
		acceleration_structure_build_sizes_info.buildScratchSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
	);
	global_TLAS.scratch = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);

	VkAccelerationStructureCreateInfoKHR acceleration_structure_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = global_TLAS.buffer.handle,
		.size = acceleration_structure_build_sizes_info.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
	};
	VK_CHECK(vkCreateAccelerationStructureKHR(context.device, &acceleration_structure_info, nullptr,
		&global_TLAS.handle));

	acceleration_structure_build_geometry_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	acceleration_structure_build_geometry_info.dstAccelerationStructure = global_TLAS.handle;
	acceleration_structure_build_geometry_info.scratchData = VkUtils::GetDeviceAddress(context.device,
		global_TLAS.scratch.handle);

	VkAccelerationStructureBuildRangeInfoKHR acceleration_structure_build_range_info {
		.primitiveCount = 1,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0
	};

	VkAccelerationStructureBuildRangeInfoKHR *acceleration_structure_build_range_pinfo =
		&acceleration_structure_build_range_info;
	VkUtils::ExecuteOneTimeCommands(context.device, context.graphics_queue, context.command_pool,
		[&](VkCommandBuffer command_buffer) {
			vkCmdBuildAccelerationStructuresKHR(command_buffer,
				1,
				&acceleration_structure_build_geometry_info,
				&acceleration_structure_build_range_pinfo
			);
		}
	);

	VkUtils::DestroyGPUBuffer(context.allocator, instances_buffer);
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

