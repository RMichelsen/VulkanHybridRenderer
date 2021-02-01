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
	TransientResource CreateTransientTexture(const char *name, VkFormat format);
	TransientResource CreateTransientTexture(const char *name, uint32_t width, uint32_t height, VkFormat format);
};
