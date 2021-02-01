#pragma once

class VulkanContext;
class ResourceManager {
public:
	ResourceManager(VulkanContext &context);
	~ResourceManager();

	Texture CreateTransientTexture(uint32_t width, uint32_t height, VkFormat format);
	int LoadTextureFromData(uint32_t width, uint32_t height, uint8_t *data);
	void UpdateDescriptors();
	void UploadDataToGPUBuffer(GPUBuffer buffer, void *data, VkDeviceSize size);
	void UpdatePerFrameUBO(uint32_t resource_idx, PerFrameData per_frame_data);

	GPUBuffer global_vertex_buffer;
	GPUBuffer global_index_buffer;

	std::vector<Texture> textures;
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

	VulkanContext &context;
};