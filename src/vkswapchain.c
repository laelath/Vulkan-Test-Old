#include "vkswapchain.h"
#include "vktools.h"

#define GET_INSTANCE_PROC_ADDR(swapchain, instance, entrypoint) \
{ \
	swapchain->fp##entrypoint = (PFN_vk##entrypoint)vkGetInstanceProcAddr(instance, "vk" #entrypoint); \
	if (swapchain->fp##entrypoint == NULL) \
		ERR_EXIT("vkGetInstanceProcAddr failed to find vk" #entrypoint ".\nExiting...\n"); \
}

#define GET_DEVICE_PROC_ADDR(swapchain, device, entrypoint) \
{ \
	swapchain->fp##entrypoint = (PFN_vk##entrypoint)vkGetDeviceProcAddr(device, "vk" #entrypoint); \
	if (swapchain->fp##entrypoint == NULL) \
		ERR_EXIT("vkGetDeviceProcAddr failed to find vk" #entrypoint ".\nExiting...\n"); \
}

void loadInstanceFunctions(Swapchain *swapchain, VkInstance instance)
{
	GET_INSTANCE_PROC_ADDR(vkData, GetPhysicalDeviceSurfaceSupportKHR);
	GET_INSTANCE_PROC_ADDR(vkData, GetPhysicalDeviceSurfaceCapabilitiesKHR);
	GET_INSTANCE_PROC_ADDR(vkData, GetPhysicalDeviceSurfaceFormatsKHR);
	GET_INSTANCE_PROC_ADDR(vkData, GetPhysicalDeviceSurfacePresentModesKHR);
	//GET_INSTANCE_PROC_ADDR(vkData, CreateSwapchainKHR);
	//GET_INSTANCE_PROC_ADDR(vkData, DestroySwapchainKHR);
	//GET_INSTANCE_PROC_ADDR(vkData, GetSwapchainImagesKHR);
	//GET_INSTANCE_PROC_ADDR(vkData, AcquireNextImageKHR);
	//GET_INSTANCE_PROC_ADDR(vkData, QueuePresentKHR);
}

void loadDeviceFunctions(Swapchain *swapchain, VkDevice device)
{
	GET_DEVICE_PROC_ADDR(vkData, CreateSwapchainKHR);
	GET_DEVICE_PROC_ADDR(vkData, DestroySwapchainKHR);
	GET_DEVICE_PROC_ADDR(vkData, GetSwapchainImagesKHR);
	GET_DEVICE_PROC_ADDR(vkData, AcquireNextImageKHR);
	GET_DEVICE_PROC_ADDR(vkData, QueuePresentKHR);
}

void createSurface(Swapchain *swapchain, GLFWwindow *window)
{
	glfwCreateWindowSurface(swapchain->instance, window, NULL, &swapchain->surface);

	uint32_t formatCount;
	VK_CHECK(swapchain->fpGetPhysicalDeviceSurfaceFormatsKHR(swapchain->physicalDevice, swapchain->surface,
				&formatCount, NULL));
	if (formatCount == 0) ERR_EXIT("No surface formats were found.\nExiting...\n");

	VkSurfaceFormatKHR *surfaceFormats = malloc(formatCount * sizeof(VkSurfaceFormatKHR));
	VK_CHECK(swapchain->fpGetPhysicalDeviceSurfaceFormatsKHR(swapchain->physicalDevice, swapchain->surface,
				&formatCount, surfaceFormats));

	if (formatCount == 1 && surfaceFormats[0].format == VK_FORMAT_UNDEFINED)
		swapchain->format = VK_FORMAT_B8G8R8A8_UNORM;
	else
		swapchain->format = surfaceFormats[0].format;

	swapchain->colorSpace = surfaceFormats[0].colorSpace;

	free(surfaceFormats);
}

uint32_t getSwapchainQueueIndex(Swapchain *swapchain)
{
	uint32_t queueCount;
	vkGetPhysicalDeviceQueueFamilyProperties(swapchain->physicalDevice, queueCount, NULL);
	if(queueCount == 0) ERR_EXIT("No device queue was found.\nExiting...\n");

	VkQueueFamilyProperties *queueProps = malloc(queueCount * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(swapchain->physicalDevice, queueCount, queueProps);

	VkBool32 *supportsPresent = malloc(queueCount * sizeof(VkBool32));
	for (uint32_t i = 0; i < queueCount; ++i)
		swapchain->fpGetPhysicalDeviceSurfaceSupportKHR(swapchain->physicalDevice, i, swapchain->surface,
				&supportsPresent[i]);

	uint32_t graphicsQueueNodeIndex = UINT32_MAX;
	uint32_t presentQueueNodeIndex = UINT32_MAX;
	bool foundQueue = false;
	for (uint32_t i = 0; i < queueCount && !foundQueue; ++i) {
		if ((queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			if (graphicsQueueNodeIndex == UINT32_MAX)
				graphicsQueueNodeIndex = i;

			if (supportsPresent[i] == VK_TRUE) {
				graphicsQueueNodeIndex = i;
				presentQueueNodeIndex = i;
				foundQueue = true;
			}
		}
	}

	if (presentQueueNodeIndex == UINT32_MAX) {
		foundQueue = false;
		for (uint32_t i = 0; i < queueCount && !foundQueue; ++i) {
			if (supportsPresent[i] == VK_TRUE) {
				presentQueueNodeIndex = i;
				foundQueue = true;
			}
		}
	}

	free(supportsPresent);
	free(queueProps);

	if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX)
		ERR_EXIT("Could not find graphics and present queues.\nExiting...\n");
	//Possible to use separate queues for graphics and present, but this implementation doesn't incude that
	if (graphicsQueueNodeIndex != presentQueueNodeIndex)
		ERR_EXIT("Could not find a common graphics and present queue.\nExiting...\n");

	return graphicsQueueNodeIndex;
}

void setupSwapchainBuffers(Swapchain *swapchain)
{
	VkSwapchainKHR oldSwapchain = swapchain->swapchain;

	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	VK_CHECK(vkData->fpGetPhysicalDeviceSurfaceCapabilitiesKHR(swapchain->physicalDevice, swapchain->surface,
				&surfaceCapabilities));

	VkExtent2D swapchainExtent;
	if(surfaceCapabilities.currentExtent.width == (uint32_t)-1) {
		swapchainExtent.width = swapchain->width;
		swapchainExtent.height = swapchain->height;
	} else {
		swapchainExtent = surfaceCapabilities.currentExtent;
		swapchain->width = surfaceCapabilities.currentExtent.width;
		swapchain->height = surfaceCapabilities.currentExtent.height;
	}

	//printf("Creating surface, width: %u heiht: %u\n", vkData->width, vkData->height);

	/*uint32_t presentModeCount;
	VK_CHECK(vkData->fpGetPhysicalDeviceSurfacePresentModesKHR(vkData->physicalDevice, vkData->surface,
	&presentModeCount, NULL));

	VkPresentModeKHR *presentModes = malloc(presentModeCount * sizeof(VkPresentModeKHR));
	VK_CHECK(vkData->fpGetPhysicalDeviceSurfacePresentModesKHR(vkData->physicalDevice, vkData->surface,
	&presentModeCount, presentModes));*/

	VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
	//VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
	//VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	//VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;

	uint32_t desiredNumberOfSwapchainImages = surfaceCapabilities.minImageCount + 1;
	if (surfaceCapabilities.maxImageCount > 0 &&
			desiredNumberOfSwapchainImages > surfaceCapabilities.maxImageCount)
		desiredNumberOfSwapchainImages = surfaceCapabilities.maxImageCount;

	VkSurfaceTransformFlagsKHR preTransform;
	if (surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	else 
		preTransform = surfaceCapabilities.currentTransform;

	VkSwapchainCreateInfoKHR swapchainInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.pNext = NULL,
		.flags = 0,
		.surface = swapchain->surface,
		.minImageCount = desiredNumberOfSwapchainImages,
		.imageFormat = swapchain->format,
		.imageColorSpace = swapchain->colorSpace,
		.imageExtent = swapchainExtent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = NULL,
		.preTransform = preTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = swapchainPresentMode,
		.clipped = VK_TRUE,
		.oldSwapchain = oldSwapchain
	};

	VK_CHECK(swapchain->fpCreateSwapchainKHR(swapchain->device, &swapchainInfo, NULL, &swapchain->swapchain));

	if (oldSwapchain != VK_NULL_HANDLE) {
		swapchain->fpDestroySwapchainKHR(swapchain->device, oldSwapchain, NULL);
	}

	VK_CHECK(swapchain->fpGetSwapchainImagesKHR(swapchain->device, swapchain->swapchain, &swapchain->imageCount,
				NULL));

	VkImage *swapchainImages = malloc(swapchain->imageCount * sizeof(VkImage));
	VK_CHECK(swapchain->fpGetSwapchainImagesKHR(swapchain->device, swapchain->swapchain, &swapchain->imageCount,
				swapchainImages));

	swapchain->buffers = malloc(swapchain->imageCount * sizeof(SwapchainBuffers));

	for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i) {
		swapchain->buffers[i].image = swapchainImages[i];
		//setImageLayout(vkData->setupCmdBuffer, vkData->buffers.images[i], VK_IMAGE_ASPECT_COLOR_BIT,
		//VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		VkImageViewCreateInfo colorAttachmentView = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.image = swapchain->buffers[i].image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = swapchain->format,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY },
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1 }
		};

		VK_CHECK(vkCreateImageView(device, &colorAttachmentView, NULL, &swapchain->buffers[i].view));
	}

	free(swapchainImages);
}

uint32_t acquireNextImage(Swapchain *swapchain, uint64_t timeout, VkSemaphore waitSemaphore)
{
	VK_CHECK(swapchain->fpAcquireNextImageKHR(swapchain->device, swapchain->swapchain, timeout, waitSemaphore,
				NULL, &swapchain->currentBuffer));
	return swapchain->currentBuffer;
}

void presentQueue(Swapchain *swapchain, VkSemaphore waitSemaphore)
{
	VkPresntInfoKHR presentInfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext = NULL,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &waitSemaphore,
		.swapchainCount = 1,
		.pSwapchains = &swapchain->swapchain,
		.pImageIndices = &swapchain->currentBuffer,
		.pResults = NULL
	};

	VK_CHECK(swapchain->fpQueuePresentKHR(swapchain->queue, &presentInfo));
}

void resizeSwapchain(Swapchain *swapchain)
{
	printf("Resizing window.\n");
	destroySwapchain(swapchain);

	VK_CHECK(vkQueueWaitIdle(swapchain->queue));
	VK_CHECK(vkDeviceWaitIdle(swapchain->device));

	prepare(vkData);
}

void destroySwapchain(Swapchain *swapchain)
{
	for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i)
		vkDestroyFramebuffer(vkData->device, vkData->framebuffers[i], NULL);

	if (vkData->setupCmdBuffer != VK_NULL_HANDLE)
		vkFreeCommandBuffers(vkData->device, vkData->cmdPool, 1, &vkData->setupCmdBuffer);

	vkFreeCommandBuffers(vkData->device, vkData->cmdPool, vkData->swapchainImageCount, vkData->buffers.cmdBuffers);
	vkDestroyCommandPool(vkData->device, vkData->cmdPool, NULL);

	vkDestroyPipeline(vkData->device, vkData->pipeline, NULL);
	vkDestroyRenderPass(vkData->device, vkData->renderPass, NULL);

	vkDestroyBuffer(vkData->device, vkData->vertices.buffer, NULL);
	vkFreeMemory(vkData->device, vkData->vertices.memory, NULL);

	vkDestroyBuffer(vkData->device, vkData->indices.buffer, NULL);
	vkFreeMemory(vkData->device, vkData->indices.memory, NULL);
	
	vkDestroySemaphore(vkData->device, vkData->semaphores.presentComplete, NULL);
	vkDestroySemaphore(vkData->device, vkData->semaphores.renderComplete, NULL);

	for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i)
		vkDestroyImageView(vkData->device, vkData->buffers.imageViews[i], NULL);

	free(vkData->buffers.images);
	free(vkData->buffers.cmdBuffers);
	free(vkData->buffers.imageViews);
}
