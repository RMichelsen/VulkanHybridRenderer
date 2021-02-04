#pragma once

class VulkanContext;
class ResourceManager {
public:
	ResourceManager(VulkanContext &context);
	~ResourceManager();

	Texture CreateTransientTexture(uint32_t width, uint32_t height, VkFormat format);
	int LoadTextureFromData(uint32_t width, uint32_t height, uint8_t *data);

	void UpdateGeometry(std::vector<Vertex> &vertices, std::vector<uint32_t> &indices,
		Scene &scene);
	void UpdateDescriptors();
	void UpdatePerFrameUBO(uint32_t resource_idx, PerFrameData per_frame_data);

	GPUBuffer global_vertex_buffer;		  // Layout(set = 0, binding = 0)
	GPUBuffer global_index_buffer;	      // Layout(set = 0, binding = 1)
	GPUBuffer global_obj_data_buffer;     // Layout(set = 0, binding = 2)
	std::vector<Texture> textures;		  // Layout(set = 0, binding = 3)
	
	GPUBuffer global_scratch_buffer;
	GPUBuffer global_blas_buffer;
	GPUBuffer global_tlas_buffer;
	VkAccelerationStructureKHR global_blas;
	VkAccelerationStructureKHR global_tlas;
	GPUBuffer global_transform_buffer;
	GPUBuffer global_instances_buffer;

	VkDescriptorPool global_descriptor_pool = VK_NULL_HANDLE;
	VkDescriptorSetLayout global_descriptor_set_layout = VK_NULL_HANDLE;
	VkDescriptorSet global_descriptor_set = VK_NULL_HANDLE;

	VkDescriptorPool per_frame_descriptor_pool = VK_NULL_HANDLE;
	VkDescriptorSetLayout per_frame_descriptor_set_layout = VK_NULL_HANDLE;
	VkDescriptorSet per_frame_descriptor_set = VK_NULL_HANDLE;
	std::array<MappedBuffer, MAX_FRAMES_IN_FLIGHT> per_frame_ubos;

	VkDescriptorPool transient_descriptor_pool = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;

private:
	void CreateGlobalDescriptorSet();
	void CreatePerFrameDescriptorSet();
	void CreatePerFrameUBOs();
	void UpdateBLAS(uint32_t vertex_count, uint32_t primitive_count);
	void UpdateTLAS(std::vector<Primitive> &primitives);
	void UploadDataToGPUBuffer(GPUBuffer buffer, void *data, VkDeviceSize size);

	VulkanContext &context;

	PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
	PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
	PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;

	//PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessegnerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
	//	vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
};