#include "pch.h"
#include "vulkan_context.h"

#include "rendering_backend/vulkan_utils.h"

#ifndef NDEBUG
inline constexpr std::array<const char*, 1> LAYERS { 
	"VK_LAYER_KHRONOS_validation" 
};
inline constexpr std::array<const char*, 3> INSTANCE_EXTENSIONS { 
	VK_KHR_SURFACE_EXTENSION_NAME,
	VK_KHR_WIN32_SURFACE_EXTENSION_NAME,

	VK_EXT_DEBUG_UTILS_EXTENSION_NAME
};
#else
inline constexpr std::array<const char*, 0> LAYERS {};
inline constexpr std::array<const char*, 3> INSTANCE_EXTENSIONS { 
	VK_KHR_SURFACE_EXTENSION_NAME,
	VK_KHR_WIN32_SURFACE_EXTENSION_NAME,

	VK_EXT_DEBUG_UTILS_EXTENSION_NAME
};
#endif
inline constexpr std::array<const char*, 9> DEVICE_EXTENSIONS {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
	VK_KHR_SPIRV_1_4_EXTENSION_NAME,
	
	VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
	VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,

	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
	VK_KHR_RAY_QUERY_EXTENSION_NAME
};

#define VK_CHECK(x) if((x) != VK_SUCCESS) { 			\
	assert(false); 										\
	printf("Vulkan error: %s:%i", __FILE__, __LINE__); 	\
}

VulkanContext::VulkanContext(HINSTANCE hinstance, HWND hwnd) : hwnd(hwnd) {
	VK_CHECK(volkInitialize());

	VkApplicationInfo application_info {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.apiVersion = VK_API_VERSION_1_2
	};
	VkInstanceCreateInfo instance_info {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &application_info,
		.enabledLayerCount = static_cast<uint32_t>(LAYERS.size()),
		.ppEnabledLayerNames = LAYERS.data(),
		.enabledExtensionCount = static_cast<uint32_t>(INSTANCE_EXTENSIONS.size()),
		.ppEnabledExtensionNames = INSTANCE_EXTENSIONS.data()
	};
	auto d = vkCreateInstance(&instance_info, nullptr, &instance);
	volkLoadInstance(instance);

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
	volkLoadDevice(device);

	InitAllocator();

	VkCommandPoolCreateInfo command_pool_info {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
				 VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = gpu.graphics_family_idx	
	};
	VK_CHECK(vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool));

	InitFrameResources();
	InitSwapchain();
}

void VulkanContext::DestroyResources() {
	VK_CHECK(vkDeviceWaitIdle(device));

	for(FrameResources &resources : frame_resources) {
		vkDestroyFramebuffer(device, resources.framebuffer, nullptr);
		vkDestroySemaphore(device, resources.image_available, nullptr);
		vkDestroySemaphore(device, resources.render_finished, nullptr);
		vkDestroyFence(device, resources.fence, nullptr);
	}

	for(VkImageView &image_view : swapchain.image_views) {
		vkDestroyImageView(device, image_view, nullptr);
	}
	vkDestroySwapchainKHR(device, swapchain.handle, nullptr);

	vkDestroyCommandPool(device, command_pool, nullptr);
	vmaDestroyAllocator(allocator);

	vkDestroyDevice(device, nullptr);

#ifndef NDEBUG
	vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
#endif
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyInstance(instance, nullptr);
}

void VulkanContext::Resize() {
	InitSwapchain();
}

#ifndef NDEBUG
VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT /*message_severity*/,
		VkDebugUtilsMessageTypeFlagsEXT /*message_type*/,
		const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* /*user_data*/) {

	printf("%s\n", callback_data->pMessage);
	return VK_FALSE;
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

	VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &debug_utils_messenger_info,
		nullptr, &debug_messenger));
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

	gpu.raytracing_properties = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
	};
	gpu.properties = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &gpu.raytracing_properties
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

	VkPhysicalDeviceRayQueryFeaturesKHR device_ray_query_features {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
		.rayQuery = VK_TRUE
	};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR device_as_features {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
		.pNext = &device_ray_query_features,
		.accelerationStructure = VK_TRUE
	};
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR device_rt_pipeline_features {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
		.pNext = &device_as_features,
		.rayTracingPipeline = VK_TRUE
	};
	VkPhysicalDeviceVulkan12Features device_vk12_features {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &device_rt_pipeline_features,
		.descriptorIndexing = VK_TRUE,
		.shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
		.descriptorBindingPartiallyBound = VK_TRUE,
		.descriptorBindingVariableDescriptorCount = VK_TRUE,
		.runtimeDescriptorArray = VK_TRUE,
		.scalarBlockLayout = VK_TRUE,
		.bufferDeviceAddress = VK_TRUE
	};
	VkPhysicalDeviceFeatures2 device_features {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &device_vk12_features,
		.features = VkPhysicalDeviceFeatures {
			.samplerAnisotropy = VK_TRUE,
			.shaderStorageImageReadWithoutFormat = VK_TRUE,
			.shaderStorageImageWriteWithoutFormat = VK_TRUE
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
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = gpu.handle,
		.device = device,
		.preferredLargeHeapBlockSize = 1024 * 1024 * 1024, // 1GB
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
	VkCommandBufferAllocateInfo command_buffer_info {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	for(FrameResources &resources : frame_resources) {
		VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_info, &resources.command_buffer));
		VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &resources.image_available));
		VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &resources.render_finished));
		VK_CHECK(vkCreateFence(device, &fence_info, nullptr, &resources.fence));
	}	
}

void VulkanContext::InitSwapchain() {
	VK_CHECK(vkDeviceWaitIdle(device));
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
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
		VkImageViewCreateInfo image_view_info = VkUtils::ImageViewCreateInfo2D(
			swapchain.images[i], swapchain.format);
		VK_CHECK(vkCreateImageView(device, &image_view_info, nullptr, 
			&swapchain.image_views[i]));
	}
}

