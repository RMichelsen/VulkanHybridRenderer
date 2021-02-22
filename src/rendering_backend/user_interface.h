#pragma once

class Renderer;
class RenderGraph;
class ResourceManager;
class VulkanContext;
class UserInterface {
public:
	UserInterface(VulkanContext &context, ResourceManager &resource_manager);
	void DestroyResources();

	void Update(Renderer &renderer);
	void Draw(ResourceManager& resource_manager, VkCommandBuffer command_buffer,
		uint32_t resource_idx, uint32_t image_idx);

	void ResizeToSwapchain();
	bool IsHoveringAnchor();
	void MouseMove(float x, float y);
	void MouseLeftButtonDown();
	void MouseLeftButtonUp();
	void MouseRightButtonDown();
	void MouseRightButtonUp();
	void MouseScroll(float delta);

	float split_view_anchor;

private:
	void CreateImGuiRenderPass();
	void CreateImGuiPipeline(ResourceManager& resource_manager);

	VulkanContext &context;

	std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> framebuffers;
	VkPipeline pipeline;
	VkPipelineLayout pipeline_layout;
	VkRenderPass render_pass;

	MappedBuffer vertex_buffer;
	MappedBuffer index_buffer;
	uint32_t font_texture;

	uint64_t global_time;
	uint64_t performance_frequency;
	bool split_view_anchor_drag_active;
};

