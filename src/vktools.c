#include <stdio.h>

#include "vktools.h"

const char * getVkResultString(VkResult err)
{
	switch (err)
	{
#define STR(r) case VK_##r: return #r
		STR(SUCCESS);
		STR(NOT_READY);
		STR(TIMEOUT);
		STR(EVENT_SET);
		STR(EVENT_RESET);
		STR(INCOMPLETE);
		STR(ERROR_OUT_OF_HOST_MEMORY);
		STR(ERROR_OUT_OF_DEVICE_MEMORY);
		STR(ERROR_INITIALIZATION_FAILED);
		STR(ERROR_DEVICE_LOST);
		STR(ERROR_MEMORY_MAP_FAILED);
		STR(ERROR_LAYER_NOT_PRESENT);
		STR(ERROR_EXTENSION_NOT_PRESENT);
		STR(ERROR_FEATURE_NOT_PRESENT);
		STR(ERROR_INCOMPATIBLE_DRIVER);
		STR(ERROR_TOO_MANY_OBJECTS);
		STR(ERROR_FORMAT_NOT_SUPPORTED);
		STR(ERROR_SURFACE_LOST_KHR);
		STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
		STR(SUBOPTIMAL_KHR);
		STR(ERROR_OUT_OF_DATE_KHR);
		STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
		STR(ERROR_VALIDATION_FAILED_EXT);
		STR(ERROR_INVALID_SHADER_NV);
#undef STR
		default:
			return "UNKNOWN_ERROR";
	}
}

void setImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout)
{
	VkResult err;

	VkImageMemoryBarrier imageMemoryBarrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = NULL,
		.srcAccessMask = 0,
		.dstAccessMask = 0,
		.oldLayout = oldImageLayout,
		.newLayout = newImageLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {aspectMask, 0, 1, 0, 1}
	};

	if (newImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	if (newImageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	if (newImageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	if (newImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

	VkPipelineStageFlags srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags dstStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

	vkCmdPipelineBarrier(cmdBuffer, srcStages, dstStages, 0, 0, NULL, 0, NULL, 1, &imageMemoryBarrier);
}

bool getMemoryTypeIndex(VkPhysicalDeviceMemoryProperties memoryProps, uint32_t typeBits, VkFlags requirementsMask, uint32_t *typeIndex)
{
	for (uint32_t i = 0; i < 32; i++)
	{
		if ((typeBits & 1) == 1)
		{
			if ((memoryProps.memoryTypes[i].propertyFlags & requirementsMask) == requirementsMask)
			{
				*typeIndex = i;
				return true;
			}
		}
		typeBits >>= 1;
	}
}

VkCommandBuffer getCommandBuffer(VkDevice device, VkCommandPool cmdPool, bool begin)
{
	VkCommandBufferAllocateInfo cmdBufferAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = NULL,
		.commandPool = cmdPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	VkCommandBuffer cmdBuffer;
	VK_CHECK(vkAllocateCommandBuffers(device, &cmdBufferAllocInfo, &cmdBuffer));

	if (begin)
	{
		VkCommandBufferBeginInfo cmdBufferInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.pNext = NULL,
			.flags = 0,
			.pInheritanceInfo = NULL
		};
		VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &cmdBufferInfo));
	}

	return cmdBuffer;
}

void flushCommandBuffer(VkDevice device, VkQueue queue, VkCommandPool cmdPool, VkCommandBuffer cmdBuffer)
{
	assert(cmdBuffer != NULL);

	VK_CHECK(vkEndCommandBuffer(cmdBuffer));

	VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = NULL,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = NULL,
		.pWaitDstStageMask = NULL,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmdBuffer,
		.signalSemaphoreCount = 0,
		.pSignalSemaphores = NULL
	};

	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
	VK_CHECK(vkQueueWaitIdle(queue));

	vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
}
