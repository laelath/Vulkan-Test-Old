#ifndef VKTOOLS_H
#define VKTOOLS_H

#include <stdbool.h>
#include <assert.h>

#include <vulkan/vulkan.h>

const char * getVkResultString(VkResult err);

#define ERR_EXIT(err_msg) \
{ \
	printf(err_msg); \
	fflush(stdout); \
	exit(1); \
}

#define VK_CHECK(f) \
{ \
	VkResult err = (f); \
	if (err != VK_SUCCESS) \
	{ \
		printf("Fatal: VkResult is \"%s\" in %s at line %d \n", getVkResultString(err), __FILE__, __LINE__); \
		assert(err == VK_SUCCESS); \
	} \
}

void setImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageAspectFlags aspectMask, VkImageLayout oldLayout, VkImageLayout newLayout);
bool getMemoryTypeIndex(VkPhysicalDeviceMemoryProperties memProps, uint32_t typeBits, VkFlags requirementsMask, uint32_t *typeIndex);

VkCommandBuffer getCommandBuffer(VkDevice device, VkCommandPool cmdPool, bool begin);
void flushCommandBuffer(VkDevice device, VkQueue queue, VkCommandPool cmdPool, VkCommandBuffer cmdBuffer);

#endif
