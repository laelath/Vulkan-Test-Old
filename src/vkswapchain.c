#include "vkswapchain.h"
#include "vktools.h"

#define GET_INSTANCE_PROC_ADDR(swapchain, instance, entrypoint) \
{ \
	swapchain->fp##entrypoint = (PFN_vk##entrypoint)vkGetInstanceProcAddr(instance, "vk" #entrypoint); \
	if (swapchain->fp##entrypoint == NULL) ERR_EXIT("vkGetInstanceProcAddr failed to find vk" #entrypoint ".\nExiting...\n"); \
}

#define GET_DEVICE_PROC_ADDR(swapchain, device, entrypoint) \
{ \
	swapchain->fp##entrypoint = (PFN_vk##entrypoint)vkGetDeviceProcAddr(device, "vk" #entrypoint); \
	if (swapchain->fp##entrypoint == NULL) ERR_EXIT("vkGetDeviceProcAddr failed to find vk" #entrypoint ".\nExiting...\n"); \
}

void loadInstanceFunctions(VkInstance instance, Swapchain *swapchain)
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

void loadDeviceFunctions(VkDevice device, Swapchain *swapchain)
{
	GET_DEVICE_PROC_ADDR(vkData, CreateSwapchainKHR);
	GET_DEVICE_PROC_ADDR(vkData, DestroySwapchainKHR);
	GET_DEVICE_PROC_ADDR(vkData, GetSwapchainImagesKHR);
	GET_DEVICE_PROC_ADDR(vkData, AcquireNextImageKHR);
	GET_DEVICE_PROC_ADDR(vkData, QueuePresentKHR);
}

void createSurface(
