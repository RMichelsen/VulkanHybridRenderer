#pragma once

struct FrameResources;
class VulkanContext;
class ResourceManager;
class Renderer {
public:
	Renderer(HINSTANCE hinstance, HWND hwnd);
	~Renderer();

	void Present();

private:
	std::unique_ptr<VulkanContext> context;
	std::unique_ptr<ResourceManager> resource_manager;
	Scene scene;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

	void Render(FrameResources &resources, uint32_t image_idx);
	void CreatePipeline();
};
