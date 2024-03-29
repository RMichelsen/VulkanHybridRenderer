#pragma once

// The global descriptor set (set = 0) is laid out as follows
// Layout(set = 0, binding = 0) global_vertex_buffer
// Layout(set = 0, binding = 1) global_index_buffer
// Layout(set = 0, binding = 2) global_obj_data_buffer
// Layout(set = 0, binding = 3) global_tlas
// Layout(set = 0, binding = 4) textures

// The second global descriptor set (set = 1) is laid out as follows
// Layout(set = 1, binding = 0) storage_images

inline constexpr uint32_t MAX_GLOBAL_RESOURCES = 2048;

class VulkanContext;
class ResourceManager {
public:
	ResourceManager(VulkanContext &context);
	void DestroyResources();

	void LoadScene(const char* scene_path);

	Image Create2DImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, 
		VkImageLayout initial_layout, VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT);

	uint32_t UploadTextureFromData(uint32_t width, uint32_t height, uint8_t *data, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM, SamplerInfo *sampler_info = nullptr);
	uint32_t UploadEmptyTexture(uint32_t width, uint32_t height, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM, SamplerInfo *sampler_info = nullptr);
	uint32_t UploadNewStorageImage(uint32_t width, uint32_t height, VkFormat format);
	void DestroyStorageImage(uint32_t id);

	void TagImage(Image &image, const char *name);
	void TagImage(uint32_t image_idx, const char *name);

	void UpdateGeometry(std::vector<Vertex> &vertices, std::vector<uint32_t> &indices, Scene &scene);
	void UpdatePerFrameUBO(uint32_t resource_idx, PerFrameData &per_frame_data);

	GPUBuffer global_vertex_buffer;
	GPUBuffer global_index_buffer;
	GPUBuffer global_obj_data_buffer;

	AccelerationStructure global_BLAS;
	AccelerationStructure global_TLAS;

	std::array<Image, MAX_GLOBAL_RESOURCES> textures;
	VkDescriptorPool global_descriptor_pool0 = VK_NULL_HANDLE;
	VkDescriptorSetLayout global_descriptor_set_layout0 = VK_NULL_HANDLE;
	VkDescriptorSet global_descriptor_set0 = VK_NULL_HANDLE;

	std::array<Image, MAX_GLOBAL_RESOURCES> storage_images;
	VkDescriptorPool global_descriptor_pool1 = VK_NULL_HANDLE;
	VkDescriptorSetLayout global_descriptor_set_layout1 = VK_NULL_HANDLE;
	VkDescriptorSet global_descriptor_set1 = VK_NULL_HANDLE;

	VkDescriptorPool per_frame_descriptor_pool = VK_NULL_HANDLE;
	VkDescriptorSetLayout per_frame_descriptor_set_layout = VK_NULL_HANDLE;
	std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> per_frame_descriptor_sets;
	std::array<MappedBuffer, MAX_FRAMES_IN_FLIGHT> per_frame_ubos;

	VkDescriptorPool transient_descriptor_pool = VK_NULL_HANDLE;
	VkSampler default_sampler;
	std::vector<Sampler> samplers;

	Scene scene;

private:
	void CreateGlobalDescriptorSet0();
	void CreateGlobalDescriptorSet1();
	void CreatePerFrameDescriptorSet();
	void CreatePerFrameUBOs();
	void UpdateBLAS(uint32_t vertex_count, std::vector<Primitive> &primitives);
	void UpdateTLAS(std::vector<Primitive> &primitives);
	void UploadDataToGPUBuffer(GPUBuffer buffer, void *data, VkDeviceSize size);
	uint32_t UploadTexture(Image texture, VkSampler sampler);
	uint32_t UploadStorageImage(Image image);
	VkSampler GetSampler(SamplerInfo *sampler_info);

	VulkanContext &context;
};

