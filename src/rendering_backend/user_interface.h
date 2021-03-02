#pragma once

class Renderer;
class RenderGraph;
class RenderPath;
class ResourceManager;
class VulkanContext;
class UserInterface {
public:
	UserInterface(VulkanContext &context, ResourceManager &resource_manager);
	void DestroyResources();

	UserInterfaceState Update(RenderPath &active_render_path, std::vector<std::string> current_color_attachments);
	void Draw(ResourceManager& resource_manager, VkCommandBuffer command_buffer,
		uint32_t resource_idx, uint32_t image_idx);
	uint32_t SetActiveDebugTexture(VkFormat format);

	void ResizeToSwapchain();
	bool IsHoveringAnchor();
	void MouseMove(float x, float y);
	void MouseLeftButtonDown();
	void MouseLeftButtonUp();
	void MouseRightButtonDown();
	void MouseRightButtonUp();
	void MouseScroll(float delta);

private:
	void CreateImGuiRenderPass();
	void CreateImGuiPipeline(ResourceManager& resource_manager);

	VulkanContext &context;

	std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> framebuffers;
	VkPipeline pipeline;
	VkPipelineLayout pipeline_layout;
	VkRenderPass render_pass;

	std::array<MappedBuffer, MAX_FRAMES_IN_FLIGHT> vertex_buffers;
	std::array<MappedBuffer, MAX_FRAMES_IN_FLIGHT> index_buffers;
	uint32_t font_texture;

	uint32_t active_debug_texture;
	std::unordered_map<VkFormat, uint32_t> debug_textures;

	uint64_t global_time;
	uint64_t performance_frequency;
};

