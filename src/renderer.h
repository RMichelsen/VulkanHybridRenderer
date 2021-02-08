#pragma once

struct FrameResources;
class VulkanContext;
class ResourceManager;
class RenderGraph;
class Renderer {
public:
	Renderer(HINSTANCE hinstance, HWND hwnd);
	~Renderer();

	void Update();
	void Present();

private:
	std::unique_ptr<VulkanContext> context;
	std::unique_ptr<ResourceManager> resource_manager;
	std::unique_ptr<RenderGraph> render_graph;
	Scene scene;

	void Render(FrameResources &resources, uint32_t resource_idx, uint32_t image_idx);
	void CreatePipeline();
	TransientResource CreateTransientAttachmentImage(const char *name, VkFormat format);
	TransientResource CreateTransientAttachmentImage(const char *name, uint32_t width, uint32_t height, 
		VkFormat format);
	TransientResource CreateTransientSampledImage(const char *name, VkFormat format, uint32_t binding);
	TransientResource CreateTransientSampledImage(const char *name, uint32_t width, uint32_t height, 
		VkFormat format, uint32_t binding);
	TransientResource CreateTransientStorageImage(const char *name, VkFormat format, uint32_t binding);
	TransientResource CreateTransientStorageImage(const char *name, uint32_t width, uint32_t height, 
		VkFormat format, uint32_t binding);
};
