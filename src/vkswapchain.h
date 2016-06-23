#ifndef VKSWAPCHAIN_H
#define VKSWAPCHAIN_H

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

typedef struct _SwapchainBuffer {
	VkImage image;
	VkImageView view;
} SwapchainBuffer;

typedef struct _Swapchain {
	VkSurfaceKHR surface;
	VkFormat format;
	VkColorSpaceKHR colorSpace;
	VkSwapchainKHR swapchain;
	uint32_t imageCount;
	uint32_t currentBuffer;

	uint32_t width;
	uint32_t height;

	VkFramebuffer *framebuffers;
	SwapchainBuffer buffers;

	PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
	PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
	PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
	PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
	PFN_vkQueuePresentKHR fpQueuePresentKHR;
} Swapchain;

void loadInstanceFunctions(VkInstance instance, Swapchain *swapchain);
void loadDeviceFunctions(VkDevice device, Swapchain *swapchain);

void createSurface(VkInstance instance, VkPhysicalDevice physicalDevice, GLFWwindow *window, Swapchain *swapchain);
uint32_t getSwapchainQueueIndex(VkPhysicalDevice physicalDevice, Swapchain *swapchain);

void setupBuffers(VkPhysicalDevice physicalDevice, VkDevice device, Swapchain *swapchain);

void resizeSwapchain(Swapchain *swapchain);

void destroySwapchain(Swapchain *swapchain);

#endif
