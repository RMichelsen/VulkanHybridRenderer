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
	std::unique_ptr<UserInterface> user_interface;

private:
	void Render(FrameResources &resources, uint32_t resource_idx, uint32_t image_idx);
	void EnableRenderPath(void (*EnableRenderPath)(VulkanContext &context, ResourceManager &resource_manager,
		RenderGraph &render_grpah));
	std::unique_ptr<VulkanContext> context;
	std::unique_ptr<ResourceManager> resource_manager;
	std::unique_ptr<RenderGraph> render_graph;
};

