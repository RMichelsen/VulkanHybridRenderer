#pragma once

class VulkanContext;
class ResourceManager {
public:
	ResourceManager(VulkanContext &context);
	~ResourceManager();

	int CreateTexture(uint32_t x, uint32_t y, uint8_t *data);
	void UpdateDescriptors();

	VertexBuffer global_vertex_buffer;
	IndexBuffer global_index_buffer;

	std::vector<Texture> textures;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

	VkSampler sampler = VK_NULL_HANDLE;

private:
	VulkanContext &context;
};