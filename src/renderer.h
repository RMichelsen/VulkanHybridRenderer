#pragma once

struct FrameResources;
class VulkanContext;
class ResourceManager;
class RenderGraph;
class UserInterface;
class Renderer {
public:
	Renderer(HINSTANCE hinstance, HWND hwnd);
	~Renderer();

	void Update();
	void Present(HWND hwnd);

private:
	void Render(FrameResources &resources, uint32_t resource_idx, uint32_t image_idx);
	TransientResource CreateTransientBackbuffer(ColorBlendState color_blend_state = ColorBlendState::Off);
	TransientResource CreateTransientAttachmentImage(const char *name, VkFormat format,
		ColorBlendState color_blend_state = ColorBlendState::Off);
	TransientResource CreateTransientAttachmentImage(const char *name, uint32_t width, uint32_t height, 
		VkFormat format, ColorBlendState color_blend_state = ColorBlendState::Off);
	TransientResource CreateTransientSampledImage(const char *name, VkFormat format, uint32_t binding);
	TransientResource CreateTransientSampledImage(const char *name, uint32_t width, uint32_t height, 
		VkFormat format, uint32_t binding);
	TransientResource CreateTransientStorageImage(const char *name, VkFormat format, uint32_t binding);
	TransientResource CreateTransientStorageImage(const char *name, uint32_t width, uint32_t height, 
		VkFormat format, uint32_t binding);

	std::unique_ptr<VulkanContext> context;
	std::unique_ptr<ResourceManager> resource_manager;
	std::unique_ptr<RenderGraph> render_graph;
	std::unique_ptr<UserInterface> user_interface;
	Scene scene;
};

