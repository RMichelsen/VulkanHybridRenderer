#pragma once

struct PhysicalDevice {
	VkPhysicalDevice handle = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties2 properties;
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR raytracing_properties;
	VkSurfaceCapabilitiesKHR surface_capabilities;
	uint32_t graphics_family_idx = UINT32_MAX;
	uint32_t compute_family_idx = UINT32_MAX;
};

struct Swapchain {
	VkSwapchainKHR handle = VK_NULL_HANDLE;
	VkFormat format;
	VkPresentModeKHR present_mode;
	VkExtent2D extent;
	std::vector<VkImage> images;
	std::vector<VkImageView> image_views;	
};

struct FrameResources {
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	VkSemaphore image_available = VK_NULL_HANDLE;
	VkSemaphore render_finished = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;
};

class VulkanContext {
public:
	VulkanContext(HINSTANCE hinstance, HWND hwnd);
	void DestroyResources();
	void Resize(HWND hwnd);

	VkInstance instance = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkQueue graphics_queue = VK_NULL_HANDLE;
	VkQueue compute_queue = VK_NULL_HANDLE;

	PhysicalDevice gpu;
	VmaAllocator allocator;
	Swapchain swapchain;
	std::array<FrameResources, MAX_FRAMES_IN_FLIGHT> frame_resources;

private:
#ifndef NDEBUG
	void InitDebugMessenger();
	VkDebugUtilsMessengerEXT debug_messenger;
#endif

	void InitPhysicalDevice();
	void InitLogicalDevice();
	void InitAllocator();
	void InitFrameResources();
	void InitSwapchain(HWND hwnd);
};

