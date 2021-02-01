#pragma once

class VulkanContext;
class ResourceManager {
public:
	ResourceManager(VulkanContext &context);
	~ResourceManager();

	Texture CreateTransientTexture(uint32_t width, uint32_t height, VkFormat format);
	int LoadTextureFromData(uint32_t width, uint32_t height, uint8_t *data);
	void UpdateDescriptors();

	VertexBuffer global_vertex_buffer;
	IndexBuffer global_index_buffer;

	VkDescriptorPool global_descriptor_pool = VK_NULL_HANDLE;
	std::vector<Texture> textures;
	VkDescriptorPool texture_array_descriptor_pool = VK_NULL_HANDLE;
	VkDescriptorSetLayout texture_array_descriptor_set_layout = VK_NULL_HANDLE;
	VkDescriptorSet texture_array_descriptor_set = VK_NULL_HANDLE;

	VkSampler sampler = VK_NULL_HANDLE;

private:
	VulkanContext &context;
};