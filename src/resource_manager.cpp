#include "pch.h"
#include "resource_manager.h"

#include "vulkan_context.h"
#include "vulkan_utils.h"

inline constexpr uint32_t MAX_TRANSIENT_TEXTURES = 256;
inline constexpr uint32_t MAX_TRANSIENT_SETS = 128;

inline constexpr uint32_t MAX_GLOBAL_TEXTURES = 1024;
inline constexpr uint32_t MAX_PER_FRAME_UBOS = MAX_FRAMES_IN_FLIGHT;

inline constexpr uint32_t MAX_VERTEX_AND_INDEX_BUFSIZE = 128 * 1024 * 1024; // 128MB
inline constexpr uint32_t MAX_BLAS_AND_SCRATCH_BUFSIZE = 256 * 1024 * 1024; // 256MB

ResourceManager::ResourceManager(VulkanContext &context) : context(context) {
	vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
		vkGetDeviceProcAddr(context.device, "vkCreateAccelerationStructureKHR"));
	vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
		vkGetDeviceProcAddr(context.device, "vkCmdBuildAccelerationStructuresKHR"));
	vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
		vkGetDeviceProcAddr(context.device, "vkGetAccelerationStructureBuildSizesKHR"));

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

	buffer_info = VkUtils::BufferCreateInfo(MAX_BLAS_AND_SCRATCH_BUFSIZE,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
	global_scratch_buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);

	buffer_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
	global_blas_buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);
	global_tlas_buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);

	VkTransformMatrixKHR blas_transform {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f
	};
	buffer_info = VkUtils::BufferCreateInfo(sizeof(VkTransformMatrixKHR),
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	global_transform_buffer = VkUtils::CreateGPUBuffer(context.allocator, buffer_info);
	UploadDataToGPUBuffer(global_transform_buffer, &blas_transform, sizeof(VkTransformMatrixKHR));
	
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
	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(static_cast<VkDeviceSize>(width) * 
		static_cast<VkDeviceSize>(height) * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

	MappedBuffer buffer = VkUtils::CreateMappedBuffer(context.allocator, buffer_info);
	memcpy(buffer.mapped_data, data, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

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

	VkImageViewCreateInfo image_view_info = VkUtils::ImageViewCreateInfo2D(texture.image, 
		VK_FORMAT_R8G8B8A8_UNORM);
	VK_CHECK(vkCreateImageView(context.device, &image_view_info, nullptr, &texture.image_view));

	textures.push_back(texture);
	return static_cast<int>(textures.size()) - 1;
}

void ResourceManager::UpdateGeometry(std::vector<Vertex> &vertices, std::vector<uint32_t> &indices,
	Scene &scene) {
	UploadDataToGPUBuffer(global_vertex_buffer, vertices.data(), vertices.size() * sizeof(Vertex));
	UploadDataToGPUBuffer(global_index_buffer, indices.data(), indices.size() * sizeof(uint32_t));

	UpdateBLAS(static_cast<uint32_t>(vertices.size()), static_cast<uint32_t>(indices.size()) / 3);

	// TODO: Support multiple meshes
	assert(scene.meshes.size() == 1);
	for(Mesh &mesh : scene.meshes) {
		UploadDataToGPUBuffer(global_obj_data_buffer, mesh.primitives.data(), 
			mesh.primitives.size() * sizeof(Primitive));

		UpdateTLAS(mesh.primitives);
	}

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

	std::array<VkWriteDescriptorSet, 4> write_descriptor_sets {
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
			.dstSet = global_descriptor_set,
			.dstBinding = 3,
			.descriptorCount = static_cast<uint32_t>(descriptors.size()),
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = descriptors.data()
		}
	};

	vkUpdateDescriptorSets(context.device, static_cast<uint32_t>(write_descriptor_sets.size()), 
		write_descriptor_sets.data(), 0, nullptr);
}

void ResourceManager::UpdatePerFrameUBO(uint32_t resource_idx, PerFrameData per_frame_data) {
	memcpy(per_frame_ubos[resource_idx].mapped_data, &per_frame_data, sizeof(PerFrameData));
}

void ResourceManager::CreateGlobalDescriptorSet() {
	std::array<VkDescriptorPoolSize, 2> descriptor_pool_sizes {
		VkDescriptorPoolSize {
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_GLOBAL_TEXTURES
		},
		VkDescriptorPoolSize {
			.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 3
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

	std::array<VkDescriptorSetLayoutBinding, 4> descriptor_set_layout_bindings {
		VkDescriptorSetLayoutBinding {
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT
		},
		VkDescriptorSetLayoutBinding {
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT
		},
		VkDescriptorSetLayoutBinding {
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT
		},
		VkDescriptorSetLayoutBinding {
			.binding = 3,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_GLOBAL_TEXTURES,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT
		},
	};
	std::array<VkDescriptorBindingFlags, 4> descriptor_binding_flags {
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

void ResourceManager::UpdateBLAS(uint32_t vertex_count, uint32_t primitive_count) {
	// TODO: Allow update of global_transform_buffer (single transform matrix)

	VkAccelerationStructureGeometryKHR acceleration_structure_geometry {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
		.geometry = VkAccelerationStructureGeometryDataKHR {
			.triangles = VkAccelerationStructureGeometryTrianglesDataKHR {
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
				.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
				.vertexData = VkUtils::GetDeviceAddressConst(context.device, global_vertex_buffer),
				.vertexStride = sizeof(Vertex),
				.maxVertex = vertex_count,
				.indexType = VK_INDEX_TYPE_UINT32,
				.indexData = VkUtils::GetDeviceAddressConst(context.device, global_index_buffer),
				.transformData = VkUtils::GetDeviceAddressConst(context.device, global_transform_buffer)
			}
		},
		.flags = VK_GEOMETRY_OPAQUE_BIT_KHR
	};
	
	VkAccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &acceleration_structure_geometry
	};

	uint32_t max_primitive_counts = 1;
	VkAccelerationStructureBuildSizesInfoKHR acceleration_structure_build_sizes_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
	};
	vkGetAccelerationStructureBuildSizesKHR(context.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &acceleration_structure_build_geometry_info,
		&max_primitive_counts, &acceleration_structure_build_sizes_info);

	VkAccelerationStructureCreateInfoKHR acceleration_structure_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = global_blas_buffer.handle,
		.size = acceleration_structure_build_sizes_info.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
	};
	VK_CHECK(vkCreateAccelerationStructureKHR(context.device, &acceleration_structure_info, nullptr,
		&global_blas));

	// TODO: Split? NECESSARY....
	VkAccelerationStructureBuildRangeInfoKHR acceleration_structure_build_range_info {
		.primitiveCount = primitive_count,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0
	};

	acceleration_structure_build_geometry_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	acceleration_structure_build_geometry_info.dstAccelerationStructure = global_blas;
	acceleration_structure_build_geometry_info.scratchData = VkUtils::GetDeviceAddress(context.device,
		global_scratch_buffer);

	std::array<VkAccelerationStructureBuildRangeInfoKHR *, 1> acceleration_structure_build_range_infos {
		&acceleration_structure_build_range_info
	};
	VkUtils::ExecuteOneTimeCommands(context.device, context.graphics_queue, context.command_pool,
		[&](VkCommandBuffer command_buffer) {
			vkCmdBuildAccelerationStructuresKHR(command_buffer, 1,
				&acceleration_structure_build_geometry_info,
				acceleration_structure_build_range_infos.data()
			);
		}
	);
}

void ResourceManager::UpdateTLAS(std::vector<Primitive> &primitives) {
	std::vector<VkAccelerationStructureInstanceKHR> acceleration_structure_instances;
	uint64_t blas_device_address =
		VkUtils::GetDeviceAddress(context.device, global_blas_buffer).deviceAddress;
	for(int i = 0; i < primitives.size(); ++i) {
		glm::vec4 row1 = glm::row(primitives[i].transform, 0);
		glm::vec4 row2 = glm::row(primitives[i].transform, 1);
		glm::vec4 row3 = glm::row(primitives[i].transform, 2);
		acceleration_structure_instances.emplace_back(VkAccelerationStructureInstanceKHR {
			.transform = VkTransformMatrixKHR {
				row1[0], row1[1], row1[2], row1[3],
				row2[0], row2[1], row2[2], row2[3],
				row3[0], row3[1], row3[2], row3[3]
			},
			.instanceCustomIndex = i,
			.mask = 0xFF,
			.instanceShaderBindingTableRecordOffset = 0,
			.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
			.accelerationStructureReference = blas_device_address
		});
	}
	VkBufferCreateInfo buffer_info = VkUtils::BufferCreateInfo(acceleration_structure_instances.size() *
		sizeof(VkAccelerationStructureInstanceKHR),
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
	UploadDataToGPUBuffer(global_instances_buffer, acceleration_structure_instances.data(),
		acceleration_structure_instances.size() * sizeof(VkAccelerationStructureBuildGeometryInfoKHR));

	VkAccelerationStructureGeometryKHR acceleration_structure_geometry {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.geometry = VkAccelerationStructureGeometryDataKHR {
			.instances = VkAccelerationStructureGeometryInstancesDataKHR {
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
				.arrayOfPointers = VK_FALSE,
				.data = VkUtils::GetDeviceAddressConst(context.device, global_instances_buffer)
			}
		},
		.flags = VK_GEOMETRY_OPAQUE_BIT_KHR
	};

	VkAccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &acceleration_structure_geometry,
		.scratchData = VkUtils::GetDeviceAddress(context.device, global_scratch_buffer)
	};

	uint32_t max_primitive_counts = 1;
	VkAccelerationStructureBuildSizesInfoKHR acceleration_structure_build_sizes_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
	};
	vkGetAccelerationStructureBuildSizesKHR(context.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &acceleration_structure_build_geometry_info,
		&max_primitive_counts, &acceleration_structure_build_sizes_info);

	VkAccelerationStructureCreateInfoKHR acceleration_structure_info {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = global_tlas_buffer.handle,
		.size = acceleration_structure_build_sizes_info.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
	};
	VK_CHECK(vkCreateAccelerationStructureKHR(context.device, &acceleration_structure_info, nullptr,
		&global_blas));

	// TODO: Split?
	VkAccelerationStructureBuildRangeInfoKHR acceleration_structure_build_range_info {
		.primitiveCount = primitive_count,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0
	};

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

