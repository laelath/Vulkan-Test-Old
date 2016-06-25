#ifndef VKSWAPCHAIN_H
#define VKSWAPCHAIN_H

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

typedef struct _SwapchainBuffers {
	VkImage image;
	VkImageView view;
} SwapchainBuffers;

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
	SwapchainBuffers buffers;

	VkPresentInfoKHR presentInfo;

	PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
	PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
	PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
	PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
	PFN_vkQueuePresentKHR fpQueuePresentKHR;

	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
	VkQueue queue;
} Swapchain;

void initSwapchainInstance(Swapchain *swapchain, VkInstance instance, VkPhysicalDevice physicalDevice);
void initSwapchainDevice(Swapchain *swapchain, VkDevice device, VkQueue queue);

void loadInstanceFunctions(Swapchain *swapchain);
void loadDeviceFunctions(Swapchain *swapchain);

void createSurface(Swapchain *swapchain, GLFWwindow *window);
uint32_t getSwapchainQueueIndex(Swapchain *swapchain);

void setupSwapchainBuffers(Swapchain *swapchain);

uint32_t acquireNextImage(Swapchain *swapchain, uint64_t timeout, VkSeamphore waitSemaphore);
void presentQueue(Swapchain *swapchain, VkSemaphore waitSemaphore);

void resizeSwapchain(Swapchain *swapchain);

void destroySwapchain(Swapchain *swapchain);

#endif
