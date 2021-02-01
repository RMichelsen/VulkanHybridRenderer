#include "pch.h"
#include "vulkan_context.h"

#include "vulkan_utils.h"

#ifndef NDEBUG
inline constexpr std::array<const char*, 1> LAYERS { 
	"VK_LAYER_KHRONOS_validation" 
};
inline constexpr std::array<const char*, 3> EXTENSIONS { 
	VK_KHR_SURFACE_EXTENSION_NAME, 
	VK_KHR_WIN32_SURFACE_EXTENSION_NAME, 
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME
};
#else
inline constexpr std::array<const char*, 0> LAYERS {};
inline constexpr std::array<const char*, 2> EXTENSIONS { 
	VK_KHR_SURFACE_EXTENSION_NAME, 
	VK_KHR_WIN32_SURFACE_EXTENSION_NAME
};
#endif
inline constexpr std::array<const char*, 1> DEVICE_EXTENSIONS {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#define VK_CHECK(x) if((x) != VK_SUCCESS) { 			\
	assert(false); 										\
	printf("Vulkan error: %s:%i", __FILE__, __LINE__); 	\
}

VulkanContext::VulkanContext(HINSTANCE hinstance, HWND hwnd) {
	VkApplicationInfo application_info {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.apiVersion = VK_API_VERSION_1_2
	};
	VkInstanceCreateInfo instance_info {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &application_info,
		.enabledLayerCount = static_cast<uint32_t>(LAYERS.size()),
		.ppEnabledLayerNames = LAYERS.data(),
		.enabledExtensionCount = static_cast<uint32_t>(EXTENSIONS.size()),
		.ppEnabledExtensionNames = EXTENSIONS.data()
	};
    VK_CHECK(vkCreateInstance(&instance_info, nullptr, &instance));

#ifndef NDEBUG
	InitDebugMessenger();
#endif

	VkWin32SurfaceCreateInfoKHR win32_surface_info {
		.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
		.hinstance = hinstance,
		.hwnd = hwnd	
	};
	VK_CHECK(vkCreateWin32SurfaceKHR(instance, &win32_surface_info, nullptr, &surface));

	InitPhysicalDevice();
	InitLogicalDevice();
	InitAllocator();

	VkCommandPoolCreateInfo command_pool_info {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
				 VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = gpu.graphics_family_idx	
	};
	VK_CHECK(vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool));

	InitFrameResources();
	InitSwapchain(hwnd);
	InitDepthResources();
	InitRenderPass();
}

VulkanContext::~VulkanContext() {
	VK_CHECK(vkDeviceWaitIdle(device));
	vkDestroyRenderPass(device, render_pass, nullptr);
}

#ifndef NDEBUG
VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT /*message_severity*/,
		VkDebugUtilsMessageTypeFlagsEXT /*message_type*/,
		const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* /*user_data*/) {

	printf("%s\n", callback_data->pMessage);
	return VK_ATTACHMENT_LOAD_OP_CLEAR;
}

void VulkanContext::InitDebugMessenger() {
	VkDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_info {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
						   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
					   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
					   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = DebugMessengerCallback
	};

	// Because we are using an extension function we need to load it ourselves.
	auto debug_utils_messenger_func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
		vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
	if(debug_utils_messenger_func != nullptr) {
		VK_CHECK(debug_utils_messenger_func(instance, &debug_utils_messenger_info, 
			nullptr, &debug_messenger));
	}
}
#endif

void VulkanContext::InitPhysicalDevice() {
	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(instance, &device_count, nullptr);	
	assert(device_count > 0 && "No Vulkan 1.2 capable devices found");

	std::vector<VkPhysicalDevice> physical_devices(device_count);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data()));

	// Find the first discrete GPU
	VkPhysicalDeviceProperties properties;
	for(VkPhysicalDevice &dev : physical_devices) {
		vkGetPhysicalDeviceProperties(dev, &properties);	
		if(properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			continue;
		}
		gpu.handle = dev;
	}

	gpu.rt_properties = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
	};
	gpu.properties = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &gpu.rt_properties
	};
	vkGetPhysicalDeviceProperties2(gpu.handle, &gpu.properties);
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu.handle, surface, 
		&gpu.surface_capabilities);

	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu.handle, &queue_family_count, nullptr);
	assert(queue_family_count > 0);

	std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(gpu.handle, &queue_family_count, 
		queue_families.data());

	for(uint32_t i = 0; i < queue_family_count; ++i) {
		if(queue_families[i].queueCount == 0) {
			continue;
		}

		if(queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			gpu.graphics_family_idx = i;
		}
		if(queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
			gpu.compute_family_idx = i;
		}
	}

	VkBool32 supports_present = VK_FALSE;
	VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(gpu.handle, 
		gpu.graphics_family_idx, surface, &supports_present));
	assert(supports_present);
}

void VulkanContext::InitLogicalDevice() {
	float priority = 1.0f;
	std::array<VkDeviceQueueCreateInfo, 2> device_queue_infos {
		VkDeviceQueueCreateInfo {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = gpu.graphics_family_idx,
			.queueCount = 1,
			.pQueuePriorities = &priority
		},
		VkDeviceQueueCreateInfo {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = gpu.compute_family_idx,
			.queueCount = 1,
			.pQueuePriorities = &priority
		}
	};

	VkPhysicalDeviceVulkan12Features device_vk12_features {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
		.descriptorBindingPartiallyBound = VK_TRUE,
		.descriptorBindingVariableDescriptorCount = VK_TRUE,
		.runtimeDescriptorArray = VK_TRUE
	};
	VkPhysicalDeviceFeatures2 device_features {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &device_vk12_features,
		.features = VkPhysicalDeviceFeatures {
			.samplerAnisotropy = VK_TRUE
		}
	};

	VkDeviceCreateInfo device_info {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &device_features,
		.queueCreateInfoCount = static_cast<uint32_t>(device_queue_infos.size()),
		.pQueueCreateInfos = device_queue_infos.data(),
		.enabledLayerCount = static_cast<uint32_t>(LAYERS.size()),
		.ppEnabledLayerNames = LAYERS.data(),
		.enabledExtensionCount = static_cast<uint32_t>(DEVICE_EXTENSIONS.size()),
		.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data(),
		.pEnabledFeatures = nullptr
	};

	VK_CHECK(vkCreateDevice(gpu.handle, &device_info, nullptr, &device));
	vkGetDeviceQueue(device, gpu.graphics_family_idx, 0, &graphics_queue);
	vkGetDeviceQueue(device, gpu.compute_family_idx, 0, &compute_queue);
}

void VulkanContext::InitAllocator() {
	VmaAllocatorCreateInfo allocator_info {
		.physicalDevice = gpu.handle,
		.device = device,
		.instance = instance,
		.vulkanApiVersion = VK_API_VERSION_1_2
	};

	vmaCreateAllocator(&allocator_info, &allocator);
}

void VulkanContext::InitFrameResources() {
	VkSemaphoreCreateInfo semaphore_info {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};
	VkFenceCreateInfo fence_info {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};
	VkCommandBufferAllocateInfo commandbuffer_info {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	for(FrameResources &resources : frame_resources) {
		VK_CHECK(vkAllocateCommandBuffers(device, &commandbuffer_info, &resources.commandbuffer));
		VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &resources.image_available));
		VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &resources.render_finished));
		VK_CHECK(vkCreateFence(device, &fence_info, nullptr, &resources.fence));
	}	
}

void VulkanContext::InitSwapchain(HWND hwnd) {
	VkSwapchainKHR old_swapchain = swapchain.handle;

	RECT client_rect;
	GetClientRect(hwnd, &client_rect);
	VkExtent2D extent {
		.width = static_cast<uint32_t>(client_rect.right),
		.height = static_cast<uint32_t>(client_rect.bottom)
	};

	uint32_t desired_image_count = gpu.surface_capabilities.minImageCount + 1;
	if(gpu.surface_capabilities.maxImageCount > 0 &&
	   desired_image_count > gpu.surface_capabilities.maxImageCount) {
		desired_image_count = gpu.surface_capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR swapchain_info {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = desired_image_count,
		.imageFormat = VK_FORMAT_B8G8R8A8_UNORM, // TODO: Hardcoded
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, // TODO: Hardcoded
		.imageExtent = extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, // TODO: Hardcoded
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR, // TODO: Hardcoded
		.clipped = VK_TRUE,
		.oldSwapchain = swapchain.handle
	};

	VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain.handle));

	swapchain.format = swapchain_info.imageFormat;
	swapchain.present_mode = swapchain_info.presentMode;
	swapchain.extent = extent;

	// Delete old swapchain
	if(old_swapchain != VK_NULL_HANDLE) {
		for(VkImageView &image_view : swapchain.image_views) {
			vkDestroyImageView(device, image_view, nullptr);
		}
		vkDestroySwapchainKHR(device, old_swapchain, nullptr);
	}

	uint32_t image_count = 0;
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain.handle, &image_count, nullptr));
	swapchain.images.resize(image_count);
	VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain.handle, &image_count, swapchain.images.data()));
	swapchain.image_views.resize(image_count);
	for(uint32_t i = 0; i < image_count; ++i) {
		VkImageViewCreateInfo image_view_info {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain.images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = swapchain.format,
			.components = VkComponentMapping {
				.r = VK_COMPONENT_SWIZZLE_R,	
				.g = VK_COMPONENT_SWIZZLE_G,	
				.b = VK_COMPONENT_SWIZZLE_B,	
				.a = VK_COMPONENT_SWIZZLE_A
			},
			.subresourceRange = VkImageSubresourceRange {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.levelCount = 1,
				.layerCount = 1
			}
		};
		VK_CHECK(vkCreateImageView(device, &image_view_info, nullptr, 
			&swapchain.image_views[i]));
	}
}

void VulkanContext::InitDepthResources() {
	VkImageCreateInfo image_info {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT,
		.extent = VkExtent3D {
			.width = swapchain.extent.width,
			.height = swapchain.extent.height,
			.depth = 1
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED	
	};
	VmaAllocationCreateInfo image_alloc_info {
		.usage = VMA_MEMORY_USAGE_GPU_ONLY
	};
	vmaCreateImage(allocator, &image_info, &image_alloc_info,
		&depth_texture.image, &depth_texture.allocation, nullptr);

	VkImageViewCreateInfo image_view_info {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = depth_texture.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT,
		.components = VkComponentMapping {
			.r = VK_COMPONENT_SWIZZLE_R,	
			.g = VK_COMPONENT_SWIZZLE_G,	
			.b = VK_COMPONENT_SWIZZLE_B,	
			.a = VK_COMPONENT_SWIZZLE_A
		},
		.subresourceRange = VkImageSubresourceRange {
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.levelCount = 1,
			.layerCount = 1
		}
	};
	VK_CHECK(vkCreateImageView(device, &image_view_info, nullptr,
		&depth_texture.image_view));

	VkUtils::ExecuteOneTimeCommands(device, graphics_queue, command_pool, 
		[&](VkCommandBuffer command_buffer) {
		VkUtils::InsertImageBarrier(command_buffer, depth_texture.image,
		VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | 
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
	});
}

void VulkanContext::InitRenderPass() {
	VkAttachmentDescription color_attachment {
		.format = swapchain.format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	};
	VkAttachmentReference color_attachment_ref {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};
	VkAttachmentDescription depth_attachment {
		.format = VK_FORMAT_D32_SFLOAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};
	VkAttachmentReference depth_attachment_ref {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};
	VkSubpassDescription subpass_desc {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attachment_ref,
		.pDepthStencilAttachment = &depth_attachment_ref
	};
	VkSubpassDependency subpass_dependency {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
	};
	std::array<VkAttachmentDescription, 2> attachments {
		color_attachment, depth_attachment
	};
	VkRenderPassCreateInfo render_pass_info {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = static_cast<uint32_t>(attachments.size()),
		.pAttachments = attachments.data(),
		.subpassCount = 1,
		.pSubpasses = &subpass_desc,
		.dependencyCount = 1,
		.pDependencies = &subpass_dependency
	};

	VK_CHECK(vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass));
}
