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
	VkImageView imageView;
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
	uint32_t enabledExtensionCount;
	const char* enabledExtensionNames[64];

	uint32_t width;
	uint32_t height;

	VkCommandPool cmdPool;
	VkCommandBuffer drawCmdBuffer;
	//VkImage *images;
	//SwapchainBuffer *buffers;
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

void prepareBuffers(VulkanData *vkData)
{
	VkResult err;

	VkSwapchainKHR oldSwapchain = vkData->swapchain;

	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	err = vkData->fpGetPhysicalDeviceSurfaceCapabilitiesKHR(vkData->physicalDevice, vkData->surface, &surfaceCapabilities);
	if (err) ERR_EXIT("Unable to query physical device surface capabilities\nExiting...\n");

	uint32_t presentModeCount;
	err = vkData->fpGetPhysicalDeviceSurfacePresentModesKHR(vkData->physicalDevice, vkData->surface, &presentModeCount, NULL);
	if (err) ERR_EXIT("Unable to query physical device surface present mode count.\nExiting...\n");

	VkPresentModeKHR *presentModes = malloc(presentModeCount * sizeof(VkPresentModeKHR));
	err = vkData->fpGetPhysicalDeviceSurfacePresentModesKHR(vkData->physicalDevice, vkData->surface, &presentModeCount, presentModes);
	if (err) ERR_EXIT("Unable to query physical device surface present modes.\nExiting...\n");

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

	VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

	uint32_t desiredNumberOfSwapchainImages = surfaceCapabilities.minImageCount + 1;
	if (surfaceCapabilities.maxImageCount > 0 && desiredNumberOfSwapchainImages > surfaceCapabilities.maxImageCount)
		desiredNumberOfSwapchainImages = surfaceCapabilities.maxImageCount;

	VkSurfaceTransformFlagsKHR preTransform;
	if (surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	else 
		preTransform = surfaceCapabilities.currentTransform;

	VkSwapchainCreateInfoKHR swapchain;
	swapchain.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchain.pNext = NULL;
	swapchain.surface = vkData->surface;
	swapchain.minImageCount = desiredNumberOfSwapchainImages;
	swapchain.imageFormat = vkData->format;
	swapchain.imageColorSpace = vkData->colorSpace;
	swapchain.imageExtent = swapchainExtent;
	swapchain.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain.preTransform = preTransform;
	swapchain.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchain.imageArrayLayers = 1;
	swapchain.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain.queueFamilyIndexCount = 0;
	swapchain.pQueueFamilyIndices = NULL;
	swapchain.presentMode = swapchainPresentMode;
	swapchain.oldSwapchain = oldSwapchain;
	swapchain.clipped = VK_TRUE;

	err = vkData->fpCreateSwapchainKHR(vkData->device, &swapchain, NULL, &vkData->swapchain);
	if (err) ERR_EXIT("Failed to create swapchain.\nExiting...\n");
}

void prepareVK(VulkanData *vkData)
{
	VkResult err;
	
	VkCommandPoolCreateInfo cmdPoolCreateInfo;
	cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolCreateInfo.pNext = NULL;
	cmdPoolCreateInfo.queueFamilyIndex = vkData->graphicsQueueNodeIndex;
	cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	err = vkCreateCommandPool(vkData->device, &cmdPoolCreateInfo, NULL, &vkData->cmdPool);
	if (err) ERR_EXIT("Failed to create command pool.\nExiting...\n");

	VkCommandBufferAllocateInfo cmdBufferAllocInfo;
	cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdBufferAllocInfo.pNext = NULL;
	cmdBufferAllocInfo.commandPool = vkData->cmdPool;
	cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdBufferAllocInfo.commandBufferCount = 1;

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
	VkApplicationInfo appInfo;
	VkInstanceCreateInfo instanceInfo;
	//VkInstance instance;
	
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pNext = NULL;
	appInfo.pApplicationName = "Vulkan Test";
	appInfo.pEngineName = "Test Engine";
	appInfo.engineVersion = 1;
	appInfo.apiVersion = VK_API_VERSION_1_0;
	
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pNext = NULL;
	instanceInfo.flags = 0;
	instanceInfo.pApplicationInfo = &appInfo;
	instanceInfo.enabledLayerCount = 0;
	instanceInfo.ppEnabledLayerNames = NULL;
	instanceInfo.enabledExtensionCount = vkData->enabledExtensionCount;//requiredExtensionCount;
	instanceInfo.ppEnabledExtensionNames = vkData->enabledExtensionNames;//requiredExtensions;

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
	VkDeviceQueueCreateInfo queue;
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.pNext = NULL;
	queue.queueFamilyIndex = vkData->graphicsQueueNodeIndex;
	queue.queueCount = 1;
	queue.pQueuePriorities = queuePriorities;

	VkDeviceCreateInfo device;
	device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device.pNext = NULL;
	device.queueCreateInfoCount = 1;
	device.pQueueCreateInfos = &queue;
	device.enabledLayerCount = 0;
	device.ppEnabledLayerNames = NULL;
	device.enabledExtensionCount = vkData->enabledExtensionCount;
	device.ppEnabledExtensionNames = vkData->enabledExtensionNames;

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
	vkDestroyDevice(vkData->device, NULL);
	vkDestroySurfaceKHR(vkData->instance, vkData->surface, NULL);
	vkDestroyInstance(vkData->instance, NULL);

	free(vkData->queueProps);
}

void initWindow(Window *window)
{
	//memset(window, 0, sizeof(window));

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
