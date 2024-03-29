#pragma once

struct FrameResources;
class VulkanContext;
class ResourceManager;
class RenderGraph;
class UserInterface;
class RenderPath;
class Renderer {
public:
	Renderer(HINSTANCE hinstance, HWND hwnd);
	~Renderer();

	void Update();
	void Present(HWND hwnd);

	std::unique_ptr<UserInterface> user_interface;

private:
	void Render(FrameResources &resources, uint32_t resource_idx, uint32_t image_idx);

	std::unique_ptr<VulkanContext> context;
	std::unique_ptr<ResourceManager> resource_manager;
	std::unique_ptr<RenderGraph> render_graph;
	std::unique_ptr<RenderPath> active_render_path;
	UserInterfaceState user_interface_state;
	int blue_noise_texture_index;
};

