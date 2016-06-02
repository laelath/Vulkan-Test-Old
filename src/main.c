#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#define ERR_EXIT(err_msg) \
{ \
	printf(err_msg); \
	fflush(stdout); \
	exit(1); \
}

#define GET_INSTANCE_PROC_ADDR(vkData, entrypoint) \
{ \
	vkData->fp##entrypoint = (PFN_vk##entrypoint)vkGetInstanceProcAddr(vkData->instance, "vk" #entrypoint); \
	if (vkData->fp##entrypoint == NULL) ERR_EXIT("vkGetInstanceProcAddr failed to find vk" #entrypoint ".\nExiting...\n"); \
}

#define GET_DEVICE_PROC_ADDR(vkData, entrypoint) \
{ \
	vkData->fp##entrypoint = (PFN_vk##entrypoint)vkGetDeviceProcAddr(vkData->device, "vk" #entrypoint); \
	if (vkData->fp##entrypoint == NULL) ERR_EXIT("vkGetDeviceProcAddr failed to find vk" #entrypoint ".\nExiting...\n");\
}

typedef struct _SwapchainBuffer {
	VkImage image;
	VkCommandBuffer cmd;
	VkImageView view;
} SwapchainBuffer;

typedef struct _VulkanData {
	VkInstance instance;
	VkDevice device;
	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceProperties physicalDeviceProps;
	VkPhysicalDeviceMemoryProperties memoryProps;
	VkQueue queue;
	uint32_t queueCount;
	uint32_t graphicsQueueNodeIndex;
	VkQueueFamilyProperties *queueProps;

	VkSurfaceKHR surface;
	VkFormat format;
	VkColorSpaceKHR colorSpace;
	VkSwapchainKHR swapchain;
	uint32_t swapchainImageCount;
	uint32_t currentBuffer;

	uint32_t enabledExtensionCount;
	const char* enabledExtensionNames[64];

	uint32_t width;
	uint32_t height;

	VkCommandPool cmdPool;
	VkCommandBuffer drawCmdBuffer;
	//VkImage *images;
	SwapchainBuffer *buffers;

	PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
	PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
	PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
	PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
	PFN_vkQueuePresentKHR fpQueuePresentKHR;
} VulkanData;

typedef struct _Window {
	GLFWwindow* glfwWindow;
	VulkanData vkData;
	//uint32_t width, height;
} Window;

void glfw_error_callback(int error, const char* description)
{
	printf("GLFW error code: %i\n%s\n", error, description);
	fflush(stdout);
}

void setImageLayout(VulkanData *vkData, VkImage image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout)
{

}

void prepareBuffers(VulkanData *vkData)
{
	VkResult err;

	VkSwapchainKHR oldSwapchain = vkData->swapchain;

	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	err = vkData->fpGetPhysicalDeviceSurfaceCapabilitiesKHR(vkData->physicalDevice, vkData->surface, &surfaceCapabilities);
	if (err) ERR_EXIT("Unable to query physical device surface capabilities\nExiting...\n");

	VkExtent2D swapchainExtent;
	if(surfaceCapabilities.currentExtent.width == (uint32_t)-1)
	{
		swapchainExtent.width = vkData->width;
		swapchainExtent.height = vkData->height;
	}
	else
	{
		swapchainExtent = surfaceCapabilities.currentExtent;
		vkData->width = surfaceCapabilities.currentExtent.width;
		vkData->height = surfaceCapabilities.currentExtent.height;
	}

	/*uint32_t presentModeCount;
	err = vkData->fpGetPhysicalDeviceSurfacePresentModesKHR(vkData->physicalDevice, vkData->surface, &presentModeCount, NULL);
	if (err) ERR_EXIT("Unable to query physical device surface present mode count.\nExiting...\n");

	VkPresentModeKHR *presentModes = malloc(presentModeCount * sizeof(VkPresentModeKHR));
	err = vkData->fpGetPhysicalDeviceSurfacePresentModesKHR(vkData->physicalDevice, vkData->surface, &presentModeCount, presentModes);
	if (err) ERR_EXIT("Unable to query physical device surface present modes.\nExiting...\n");*/

	VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

	uint32_t desiredNumberOfSwapchainImages = surfaceCapabilities.minImageCount + 1;
	if (surfaceCapabilities.maxImageCount > 0 && desiredNumberOfSwapchainImages > surfaceCapabilities.maxImageCount)
		desiredNumberOfSwapchainImages = surfaceCapabilities.maxImageCount;

	VkSurfaceTransformFlagsKHR preTransform;
	if (surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	else 
		preTransform = surfaceCapabilities.currentTransform;

	VkSwapchainCreateInfoKHR swapchain = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.pNext = NULL,
		.surface = vkData->surface,
		.minImageCount = desiredNumberOfSwapchainImages,
		.imageFormat = vkData->format,
		.imageColorSpace = vkData->colorSpace,
		.imageExtent = swapchainExtent,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = preTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.imageArrayLayers = 1,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = NULL,
		.presentMode = swapchainPresentMode,
		.oldSwapchain = oldSwapchain,
		.clipped = VK_TRUE
	};

	err = vkData->fpCreateSwapchainKHR(vkData->device, &swapchain, NULL, &vkData->swapchain);
	if (err) ERR_EXIT("Failed to create swapchain.\nExiting...\n");

	if (oldSwapchain != VK_NULL_HANDLE)
	{
		printf("this will crash\n");
		vkData->fpDestroySwapchainKHR(vkData->device, oldSwapchain, NULL);
	}

	err = vkData->fpGetSwapchainImagesKHR(vkData->device, vkData->swapchain, &vkData->swapchainImageCount, NULL);
	if (err) ERR_EXIT("Failed to query the number of swapchain images.\nExiting...\n");

	VkImage *swapchainImages = malloc(vkData->swapchainImageCount * sizeof(VkImage));
	err = vkData->fpGetSwapchainImagesKHR(vkData->device, vkData->swapchain, &vkData->swapchainImageCount, swapchainImages);
	if (err) ERR_EXIT("Faild to get swapchain images.\nExiting...\n");

	vkData->buffers = malloc(vkData->swapchainImageCount * sizeof(SwapchainBuffer));

	for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i)
	{
		VkImageViewCreateInfo colorAttachmentView = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext = NULL,
			.format = vkData->format,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_R,
				.g = VK_COMPONENT_SWIZZLE_G,
				.b = VK_COMPONENT_SWIZZLE_B,
				.a = VK_COMPONENT_SWIZZLE_A },
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1 },
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.flags = 0
		};

		vkData->buffers[i].image = swapchainImages[i];
		
		setImageLayout(vkData, vkData->buffers[i].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}

	vkData->currentBuffer = 0;
	free(swapchainImages);
}

void initVKSwapchain(VulkanData *vkData)
{
	VkResult err;
	
	VkCommandPoolCreateInfo cmdPoolCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = NULL,
		.queueFamilyIndex = vkData->graphicsQueueNodeIndex,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
	};

	err = vkCreateCommandPool(vkData->device, &cmdPoolCreateInfo, NULL, &vkData->cmdPool);
	if (err) ERR_EXIT("Failed to create command pool.\nExiting...\n");

	VkCommandBufferAllocateInfo cmdBufferAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = NULL,
		.commandPool = vkData->cmdPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	err = vkAllocateCommandBuffers(vkData->device, &cmdBufferAllocInfo, &vkData->drawCmdBuffer);
	if (err) ERR_EXIT("Failed to allocate draw command buffer.\nExiting...\n");

	prepareBuffers(vkData);
	//prepareDepth(vkData);
	//prepareVertices(vkData);
	//prepareDescriptorLayout(vkData);
	//prepareRenderPass(vkData);
	//preparePipeline(vkData);

	//prepareDescriptorPool(vkData);
	//prepareDescriptorSet(vkData);

	//prepareFramebuffers(vkData);
}

void initVK(VulkanData *vkData)
{
	//Query for required Vulkan extensions
	VkResult err;
	uint32_t requiredExtensionCount;
	const char** requiredExtensions;
	vkData->enabledExtensionCount = 0;
	
	requiredExtensions = glfwGetRequiredInstanceExtensions(&requiredExtensionCount);

	for (uint32_t i = 0; i < requiredExtensionCount; ++i)
		vkData->enabledExtensionNames[vkData->enabledExtensionCount++] = requiredExtensions[i];

	//vkData->enabledExtensionCount = requiredExtensionCount;
	
	//Create Vulkan Instance
	VkApplicationInfo appInfo = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pNext = NULL,
		.pApplicationName = "Vulkan Test",
		.pEngineName = "Test Engine",
		.engineVersion = 1,
		.apiVersion = VK_API_VERSION_1_0
	};
	
	VkInstanceCreateInfo instanceInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = NULL,
		.enabledExtensionCount = vkData->enabledExtensionCount,//requiredExtensionCount;
		.ppEnabledExtensionNames = vkData->enabledExtensionNames//requiredExtensions;
	};

	//Creating Vulkan Instance
	err = vkCreateInstance(&instanceInfo, NULL, &vkData->instance);
	if (err) ERR_EXIT("Failed to create Vulkan instance.\nExiting...\n");

	//Enumerating physcial devices
	uint32_t physicalDeviceCount;
	err = vkEnumeratePhysicalDevices(vkData->instance, &physicalDeviceCount, NULL);
	if (err) ERR_EXIT("Failed to query the number of physical devices present.\nExiting...\n");
	if (physicalDeviceCount == 0) ERR_EXIT("No physcial devices were found with Vulkan support.\nExiting...\n");
	
	//printf("Physical devices: %i\n", physicalDeviceCount);
	
	//Selecting the render device
	VkPhysicalDevice *physicalDevices = malloc(sizeof(VkPhysicalDevice) * physicalDeviceCount);
	err = vkEnumeratePhysicalDevices(vkData->instance, &physicalDeviceCount, physicalDevices);
	if (err) ERR_EXIT("Failed to retrieve physcial device properties.\nExiting...\n");
	
	//Just grabbing the first device present
	//VkPhysicalDevice device = physicalDevices[0];
	vkData->physicalDevice = physicalDevices[0];

	free(physicalDevices);

	//VkPhysicalDeviceProperties vkData->physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(vkData->physicalDevice, &vkData->physicalDeviceProps);
	
	/*printf("Found device: %s\n", vkData->physicalDeviceProps.deviceName);
	printf("Driver version: %d\n", vkData->physicalDeviceProps.driverVersion);
	printf("Device type: %d\n", vkData->physicalDeviceProps.deviceType);
	printf("API version: %d.%d.%d\n", 
			(vkData->physicalDeviceProps.apiVersion>>22)&0x3FF,
			(vkData->physicalDeviceProps.apiVersion>>12)&0x3FF, 
			(vkData->physicalDeviceProps.apiVersion&0xFFF));*/

	uint32_t deviceExtensionCount = 0;
	VkBool32 vkDataExtFound = VK_FALSE;
	vkData->enabledExtensionCount = 0;

	err = vkEnumerateDeviceExtensionProperties(vkData->physicalDevice, NULL, &deviceExtensionCount, NULL);
	if (err) ERR_EXIT("Unable to query device extensions.\nExiting...\n");

	if (deviceExtensionCount > 0)
	{
		VkExtensionProperties *deviceExtensionProps = malloc(deviceExtensionCount * sizeof(VkExtensionProperties));
		err = vkEnumerateDeviceExtensionProperties(vkData->physicalDevice, NULL, &deviceExtensionCount, deviceExtensionProps);
		if (err) ERR_EXIT("Unable to query device extension properties.\nExiting...\n");

		for (uint32_t i = 0; i < deviceExtensionCount && !vkDataExtFound; ++i)
		{
			if (!strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME, deviceExtensionProps[i].extensionName))
			{
				vkDataExtFound = VK_TRUE;
				vkData->enabledExtensionNames[vkData->enabledExtensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
			}
		}
	}

	GET_INSTANCE_PROC_ADDR(vkData, GetPhysicalDeviceSurfaceSupportKHR);
	GET_INSTANCE_PROC_ADDR(vkData, GetPhysicalDeviceSurfaceCapabilitiesKHR);
	GET_INSTANCE_PROC_ADDR(vkData, GetPhysicalDeviceSurfaceFormatsKHR);
	GET_INSTANCE_PROC_ADDR(vkData, GetPhysicalDeviceSurfacePresentModesKHR);
	GET_INSTANCE_PROC_ADDR(vkData, CreateSwapchainKHR);
	GET_INSTANCE_PROC_ADDR(vkData, DestroySwapchainKHR);
	GET_INSTANCE_PROC_ADDR(vkData, GetSwapchainImagesKHR);
	GET_INSTANCE_PROC_ADDR(vkData, AcquireNextImageKHR);
	GET_INSTANCE_PROC_ADDR(vkData, QueuePresentKHR);

	vkGetPhysicalDeviceQueueFamilyProperties(vkData->physicalDevice, &vkData->queueCount, NULL);
	if(vkData->queueCount == 0) ERR_EXIT("No device queue was found.\nExiting...\n");

	vkData->queueProps = malloc(vkData->queueCount * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(vkData->physicalDevice, &vkData->queueCount, vkData->queueProps);
}

void initDevice(VulkanData *vkData)
{
	VkResult err;

	float queuePriorities[1] = {0.0f};

	VkDeviceQueueCreateInfo queue = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.pNext = NULL,
		.queueFamilyIndex = vkData->graphicsQueueNodeIndex,
		.queueCount = 1,
		.pQueuePriorities = queuePriorities
	};

	VkDeviceCreateInfo device = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = NULL,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queue,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = NULL,
		.enabledExtensionCount = vkData->enabledExtensionCount,
		.ppEnabledExtensionNames = vkData->enabledExtensionNames
	};

	err = vkCreateDevice(vkData->physicalDevice, &device, NULL, &vkData->device);
	if (err) ERR_EXIT("Failed to create a Vulkan device.\nExiting...\n");

	GET_DEVICE_PROC_ADDR(vkData, CreateSwapchainKHR);
	GET_DEVICE_PROC_ADDR(vkData, DestroySwapchainKHR);
	GET_DEVICE_PROC_ADDR(vkData, GetSwapchainImagesKHR);
	GET_DEVICE_PROC_ADDR(vkData, AcquireNextImageKHR);
	GET_DEVICE_PROC_ADDR(vkData, QueuePresentKHR);
}

void initSurface(VulkanData *vkData, GLFWwindow *window)
{
	glfwCreateWindowSurface(vkData->instance, window, NULL, &vkData->surface);
	
	VkResult err;

	VkBool32 *supportsPresent = malloc(vkData->queueCount * sizeof(VkBool32));
	for (uint32_t i = 0; i < vkData->queueCount; ++i)
		vkData->fpGetPhysicalDeviceSurfaceSupportKHR(vkData->physicalDevice, i, vkData->surface, &supportsPresent[i]);

	uint32_t graphicsQueueNodeIndex = UINT32_MAX;
	uint32_t presentQueueNodeIndex = UINT32_MAX;
	VkBool32 foundQueue = 0;
	for (uint32_t i = 0; i < vkData->queueCount && !foundQueue; ++i)
	{
		if ((vkData->queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
		{
			if (graphicsQueueNodeIndex == UINT32_MAX)
				graphicsQueueNodeIndex = i;

			if (supportsPresent[i] == VK_TRUE)
			{
				graphicsQueueNodeIndex = i;
				presentQueueNodeIndex = i;
				foundQueue = 1;
			}
		}
	}

	if (presentQueueNodeIndex == UINT32_MAX)
	{
		foundQueue = 0;
		for (uint32_t i = 0; i < vkData->queueCount && !foundQueue; ++i)
		{
			if (supportsPresent[i] == VK_TRUE)
			{
				presentQueueNodeIndex = i;
				foundQueue = 1;
			}
		}
	}

	free(supportsPresent);

	if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX) ERR_EXIT("Could not find graphics and present queues.\nExiting...\n");
	if (graphicsQueueNodeIndex != presentQueueNodeIndex) ERR_EXIT("Could not find a common graphics and present queue.\nExiting...\n");

	vkData->graphicsQueueNodeIndex = graphicsQueueNodeIndex;

	initDevice(vkData);

	vkGetDeviceQueue(vkData->device, vkData->graphicsQueueNodeIndex, 0, &vkData->queue);

	uint32_t formatCount;
	err = vkData->fpGetPhysicalDeviceSurfaceFormatsKHR(vkData->physicalDevice, vkData->surface, &formatCount, NULL);
	if (err) ERR_EXIT("Unable to query physical device surface formats.\nExiting...\n");
	if (formatCount == 0) ERR_EXIT("No surface formats were found.\nExiting...\n");

	VkSurfaceFormatKHR *surfaceFormats = malloc(formatCount * sizeof(VkSurfaceFormatKHR));
	err = vkData->fpGetPhysicalDeviceSurfaceFormatsKHR(vkData->physicalDevice, vkData->surface, &formatCount, surfaceFormats);
	if (err) ERR_EXIT("Unable to retrieve physical device surface formats.\nExiting...\n");

	if (formatCount == 1 && surfaceFormats[0].format == VK_FORMAT_UNDEFINED) vkData->format = VK_FORMAT_B8G8R8A8_UNORM;
	else vkData->format = surfaceFormats[0].format;

	vkData->colorSpace = surfaceFormats[0].colorSpace;

	free(surfaceFormats);

	vkGetPhysicalDeviceMemoryProperties(vkData->physicalDevice, &vkData->memoryProps);
}

void destroyVulkan(VulkanData *vkData)
{
	//for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i)
	//	vkDestroyFramebuffer(vkData->device, vkData->framebuffers[i], NULL);
	//free(vkData->framebuffers);

	//vkDestroyDescriptorPool(vkData->device, vkData->descPool, NULL);

	//if (vkData->setupCmd)
	//	vkFreeCommandBuffers(vkData->device, vkData->cmdPool, 1, &vkData->setupCmd);

	//vkFreeCommandBuffers(vkData->device, vkData->cmdPool, 1, &vkData->drawCmd);
	//vkDestroyCommandPool(vkData->device, vkData->cmdPool, NULL);

	//vkDestroyImageView(vkData->device, vkData->depth.view, NULL);
	//vkDestroyImage(vkData->device, vkData->depth.image, NULL);
	//vkFreeMemory(vkData->device, vkData->depth.mem, NULL);
	
	vkData->fpDestroySwapchainKHR(vkData->device, vkData->swapchain, NULL);
	free(vkData->buffers);

	vkDestroyDevice(vkData->device, NULL);
	vkDestroySurfaceKHR(vkData->instance, vkData->surface, NULL);
	vkDestroyInstance(vkData->instance, NULL);

	free(vkData->queueProps);
}

void initWindow(Window *window)
{
	memset(window, 0, sizeof(Window));

	glfwSetErrorCallback(glfw_error_callback);

	if(!glfwInit()) ERR_EXIT("Cannot initialize GLFW.\nExiting...\n");
	if(!glfwVulkanSupported()) ERR_EXIT("GLFW failed to find the Vulkan loader, do you have the most recent driver?\nExiting...\n");

	//window->width = 300;
	//window->height = 300;
	window->vkData.width = 300;
	window->vkData.height = 300;

	initVK(&window->vkData);
	
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	window->glfwWindow = glfwCreateWindow(window->vkData.width, window->vkData.height, "Vulkan Test Program", NULL, NULL);
	if (!window->glfwWindow) ERR_EXIT("Failed to create GLFW window.\nExiting...\n");

	glfwSetWindowUserPointer(window->glfwWindow, window);
	//glfwSetWindowRefreshCallback(window->glfwWindow, );
	//glfwSetFramebufferSizeCallback(window->glfwWindow, );
	//glfwSetKeyCallback(window->glfwWindow, );
	
	initSurface(&window->vkData, window->glfwWindow);

	initVKSwapchain(&window->vkData);
}

void destroyWindow(Window *window)
{
	destroyVulkan(&window->vkData);
	glfwDestroyWindow(window->glfwWindow);
	glfwTerminate();
}

int main()
{
	Window window;
	initWindow(&window);

	printf("Setup complete, destroying Vulkan structures.\n");

	destroyWindow(&window);
	return 0;
}
