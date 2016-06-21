#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

//#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "vktools.h"

#define VERTEX_BUFFER_BIND_ID 0

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
	if (vkData->fp##entrypoint == NULL) ERR_EXIT("vkGetDeviceProcAddr failed to find vk" #entrypoint ".\nExiting...\n"); \
}

typedef struct _SwapchainBuffers {
	VkImage *images;
	VkCommandBuffer *cmdBuffers;
	VkImageView *imageViews;
} SwapchainBuffers;

typedef struct _VulkanData {
	VkInstance instance;
	VkDevice device;
	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceProperties physicalDeviceProps;
	VkPhysicalDeviceMemoryProperties memoryProps;
	VkQueue queue;
	VkQueueFamilyProperties *queueProps;
	uint32_t queueCount;
	uint32_t graphicsQueueNodeIndex;

	VkSurfaceKHR surface;
	VkFormat format;
	VkColorSpaceKHR colorSpace;
	VkSwapchainKHR swapchain;
	uint32_t swapchainImageCount;
	uint32_t currentBuffer;

	uint32_t enabledExtensionCount;
	const char* enabledExtensions[64];

	uint32_t width;
	uint32_t height;

	VkCommandPool cmdPool;
	VkCommandBuffer setupCmdBuffer;
	VkCommandBuffer postPresentCmdBuffer;
	VkCommandBuffer prePresentCmdBuffer;

	VkFramebuffer *framebuffers;
	SwapchainBuffers buffers;

	VkRenderPass renderPass;
	VkPipeline pipeline;
	//VkPipelineLayout pipelineLayout;
	
	struct {
		VkSemaphore presentComplete;
		VkSemaphore renderComplete;
	} semaphores;

	struct {
		VkBuffer buffer;
		VkDeviceMemory memory;
		VkPipelineVertexInputStateCreateInfo vertexInputInfo;
		VkVertexInputBindingDescription vertexInputBindings[1];
		VkVertexInputAttributeDescription vertexInputAttributes[2];
	} vertices;

	struct {
		uint32_t count;
		VkBuffer buffer;
		VkDeviceMemory memory;
	} indices;

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
} Window;

static VkShaderModule loadShader(VulkanData *vkData, char *path)
{
	FILE *shaderFile = fopen(path, "rb");
	fseek(shaderFile, 0, SEEK_END);
	size_t size = ftell(shaderFile);
	rewind(shaderFile);
	void *shaderSrc = malloc(size);
	size_t result = fread(shaderSrc, 1, size, shaderFile);
	if (result != size) ERR_EXIT("Error loading shader into memory.\nExiting...\n");

	VkShaderModuleCreateInfo moduleCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.codeSize = size,
		.pCode = shaderSrc
	};

	VkShaderModule module;
	VkResult err;
	err = vkCreateShaderModule(vkData->device, &moduleCreateInfo, NULL, &module);
	if (err) ERR_EXIT("Unable to create shader module.\nExiting...\n");

	fclose(shaderFile);
	free(shaderSrc);

	return module;
}

void setupCommandPool(VulkanData *vkData)
{
	VkResult err;

	VkCommandPoolCreateInfo cmdPoolCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = NULL,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = vkData->graphicsQueueNodeIndex
	};

	err = vkCreateCommandPool(vkData->device, &cmdPoolCreateInfo, NULL, &vkData->cmdPool);
	if (err) ERR_EXIT("Failed to create command pool.\nExiting...\n");
}

void initSetupCommandBuffer(VulkanData *vkData)
{
	VkResult err;

	VkCommandBufferAllocateInfo cmdInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = NULL,
		.commandPool = vkData->cmdPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	err = vkAllocateCommandBuffers(vkData->device, &cmdInfo, &vkData->setupCmdBuffer);
	if (err) ERR_EXIT("Failed to allocate command buffers.\nExiting...\n");

	//Start setup command buffer
	VkCommandBufferInheritanceInfo cmdInheritInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
		.pNext = NULL,
		.renderPass = VK_NULL_HANDLE,
		.subpass = 0,
		.framebuffer = VK_NULL_HANDLE,
		.occlusionQueryEnable = VK_FALSE,
		.queryFlags = 0,
		.pipelineStatistics = 0
	};

	VkCommandBufferBeginInfo cmdBeginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = NULL,
		.flags = 0,
		.pInheritanceInfo = &cmdInheritInfo
	};

	err = vkBeginCommandBuffer(vkData->setupCmdBuffer, &cmdBeginInfo);
	if (err) ERR_EXIT("Failed to start setup command buffer.\nExiting...\n");
}

void setupSwapchain(VulkanData *vkData)
{
	VkResult err;

	VkSwapchainKHR oldSwapchain = vkData->swapchain;

	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	err = vkData->fpGetPhysicalDeviceSurfaceCapabilitiesKHR(vkData->physicalDevice, vkData->surface, &surfaceCapabilities);
	if (err) ERR_EXIT("Unable to query physical device surface capabilities.\nExiting...\n");

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

	printf("Creating surface, width: %u heiht: %u\n", vkData->width, vkData->height);

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
		.flags = 0,
		.surface = vkData->surface,
		.minImageCount = desiredNumberOfSwapchainImages,
		.imageFormat = vkData->format,
		.imageColorSpace = vkData->colorSpace,
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

	err = vkData->fpCreateSwapchainKHR(vkData->device, &swapchain, NULL, &vkData->swapchain);
	if (err) ERR_EXIT("Failed to create swapchain.\nExiting...\n");

	if (oldSwapchain != VK_NULL_HANDLE)
	{
		vkData->fpDestroySwapchainKHR(vkData->device, oldSwapchain, NULL);
	}

	err = vkData->fpGetSwapchainImagesKHR(vkData->device, vkData->swapchain, &vkData->swapchainImageCount, NULL);
	if (err) ERR_EXIT("Failed to query the number of swapchain images.\nExiting...\n");

	VkImage *swapchainImages = malloc(vkData->swapchainImageCount * sizeof(VkImage));
	err = vkData->fpGetSwapchainImagesKHR(vkData->device, vkData->swapchain, &vkData->swapchainImageCount, swapchainImages);
	if (err) ERR_EXIT("Faild to get swapchain images.\nExiting...\n");

	//vkData->buffers = malloc(vkData->swapchainImageCount * sizeof(SwapchainBuffer));
	vkData->buffers.images = malloc(vkData->swapchainImageCount * sizeof(VkImage));
	vkData->buffers.cmdBuffers = malloc(vkData->swapchainImageCount * sizeof(VkCommandBuffer));
	vkData->buffers.imageViews = malloc(vkData->swapchainImageCount * sizeof(VkImageView));

	for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i)
	{
		//vkData->buffers[i].image = swapchainImages[i];
		//setImageLayout(vkData, vkData->buffers[i].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		vkData->buffers.images[i] = swapchainImages[i];
		setImageLayout(vkData->setupCmdBuffer, vkData->buffers.images[i], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		VkImageViewCreateInfo colorAttachmentView = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.image = vkData->buffers.images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = vkData->format,
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

		err = vkCreateImageView(vkData->device, &colorAttachmentView, NULL, &vkData->buffers.imageViews[i]);
		if (err) ERR_EXIT("Unable to create image view for swapchain image.\nExiting...\n");
	}

	vkData->currentBuffer = 0;
	free(swapchainImages);
}

void createCommandBuffers(VulkanData *vkData)
{
	VkCommandBufferAllocateInfo cmdBuffersInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = NULL,
		.commandPool = vkData->cmdPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = vkData->swapchainImageCount
	};

	VkResult err;

	err = vkAllocateCommandBuffers(vkData->device, &cmdBuffersInfo, vkData->buffers.cmdBuffers);
	if (err) ERR_EXIT("Unable to create swapchain image command buffers.\nExiting...\n");

	cmdBuffersInfo.commandBufferCount = 1;

	err = vkAllocateCommandBuffers(vkData->device, &cmdBuffersInfo, &vkData->postPresentCmdBuffer);
	if (err) ERR_EXIT("Unable to create post present command buffer.\nExiting...\n");

	err = vkAllocateCommandBuffers(vkData->device, &cmdBuffersInfo, &vkData->prePresentCmdBuffer);
	if (err) ERR_EXIT("Unable to create pre present command buffer.\nExiting...\n");
}

void prepareSemaphores(VulkanData *vkData)
{
	VkSemaphoreCreateInfo semaphoreInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0
	};

	VkResult err;

	err = vkCreateSemaphore(vkData->device, &semaphoreInfo, NULL, &vkData->semaphores.presentComplete);
	if (err) ERR_EXIT("Unable to create presentation completion semaphore.\nExiting...\n");

	err = vkCreateSemaphore(vkData->device, &semaphoreInfo, NULL, &vkData->semaphores.renderComplete);
	if (err) ERR_EXIT("Unable to create render completion semaphore.\nExiting...\n");
}

void prepareVertices(VulkanData *vkData)
{
	VkResult err;

	const float vertices[18] = {
		0.0f, -1.0f, 1.0f, 	1.0f, 0.0f, 0.0f,
		1.0f, 1.0f, 1.0f, 	0.0f, 1.0f, 0.0f,
		-1.0f, 1.0f, 1.0f, 	0.0f, 0.0f, 1.0f
	};

	const uint32_t indices[3] = {
		0, 1, 2
	};

	vkData->indices.count = 3;

	void *data;
	VkMemoryRequirements memReqs;
	VkMemoryAllocateInfo memAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = NULL
	};

	struct StagingBuffer {
		VkDeviceMemory memory;
		VkBuffer buffer;
	};

	struct {
		struct StagingBuffer vertices;
		struct StagingBuffer indices;
	} stagingBuffers;

	VkBufferCreateInfo vertexBufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = NULL
	};

	vertexBufferInfo.size = sizeof(vertices);
	vertexBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	err = vkCreateBuffer(vkData->device, &vertexBufferInfo, NULL, &stagingBuffers.vertices.buffer);
	if (err) ERR_EXIT("Unable to create staging vertex buffer.\nExiting...\n");

	vkGetBufferMemoryRequirements(vkData->device, stagingBuffers.vertices.buffer, &memReqs);
	memAllocInfo.allocationSize = memReqs.size;
	if (!getMemoryTypeIndex(vkData->memoryProps, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &memAllocInfo.memoryTypeIndex))
		ERR_EXIT("Unable to find suitable memory type for vertex staging buffer.\nExiting...\n");

	err = vkAllocateMemory(vkData->device, &memAllocInfo, NULL, &stagingBuffers.vertices.memory);
	if (err) ERR_EXIT("Unable to allocate memory for vertex staging buffer.\nExiting...\n");

	err = vkMapMemory(vkData->device, stagingBuffers.vertices.memory, 0, memAllocInfo.allocationSize, 0, &data);
	if (err) ERR_EXIT("Unable to map memory for vertex staging buffer.\nExiting...\n");
	memcpy(data, vertices, sizeof(vertices));
	vkUnmapMemory(vkData->device, stagingBuffers.vertices.memory);
	err = vkBindBufferMemory(vkData->device, stagingBuffers.vertices.buffer, stagingBuffers.vertices.memory, 0);
	if (err) ERR_EXIT("Unable to bind vertex staging buffer to memory.\nExiting...\n");

	vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	err = vkCreateBuffer(vkData->device, &vertexBufferInfo, NULL, &vkData->vertices.buffer);
	if (err) ERR_EXIT("Unable to create vertex buffer.\nExiting...\n");

	vkGetBufferMemoryRequirements(vkData->device, vkData->vertices.buffer, &memReqs);
	memAllocInfo.allocationSize - memReqs.size;
	if (!getMemoryTypeIndex(vkData->memoryProps, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memAllocInfo.memoryTypeIndex))
		ERR_EXIT("Unable to find suitable memory type for vertex buffer.\nExiting...\n");

	VK_CHECK(vkAllocateMemory(vkData->device, &memAllocInfo, NULL, &vkData->vertices.memory));
	VK_CHECK(vkBindBufferMemory(vkData->device, vkData->vertices.buffer, vkData->vertices.memory, 0));

	/*VkBufferCreateInfo vertexBufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.size = sizeof(vertices),
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = NULL
	};

	err = vkCreateBuffer(vkData->device, &vertexBufferInfo, NULL, &vkData->vertices.buffer);
	if (err) ERR_EXIT("Unable to create vertex buffer.\nExiting...\n");

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(vkData->device, vkData->vertices.buffer, &memReqs);

	uint32_t memoryType;
	if (!memoryTypeFromProperties(vkData, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &memoryType))
		ERR_EXIT("Unable to find suitable memory type for vertex buffer.\nExiting...\n");

	VkMemoryAllocateInfo memAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = NULL,
		.allocationSize = memReqs.size,
		.memoryTypeIndex = memoryType
	};

	err = vkAllocateMemory(vkData->device, &memAllocInfo, NULL, &vkData->vertices.memory);
	if (err) ERR_EXIT("Unable to allocate memory for vertex buffer.\nExiting...\n");

	void *data;
	err = vkMapMemory(vkData->device, vkData->vertices.memory, 0, memAllocInfo.allocationSize, 0, &data);
	if (err) ERR_EXIT("Unable to map memory for vertex buffer.\nExiting...\n");

	memcpy(data, vertices, sizeof(vertices));

	vkUnmapMemory(vkData->device, vkData->vertices.memory);
	
	err = vkBindBufferMemory(vkData->device, vkData->vertices.buffer, vkData->vertices.memory, 0);
	if (err) ERR_EXIT("Unable to bind mapped vertex memory to buffer.\nExiting...\n");

	VkBufferCreateInfo indexBufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.size = sizeof(indices),
		.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = NULL
	};

	err = vkCreateBuffer(vkData->device, &indexBufferInfo, NULL, &vkData->indices.buffer);
	if (err) ERR_EXIT("Unable to create buffer for index data.\nExiting...\n");

	vkGetBufferMemoryRequirements(vkData->device, vkData->indices.buffer, &memReqs);

	if(!memoryTypeFromProperties(vkData, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &memoryType))
		ERR_EXIT("Unable t0 find suitable memory type for index buffer.\nExiting...\n");

	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = memoryType;

	err = vkAllocateMemory(vkData->device, &memAllocInfo, NULL, &vkData->indices.memory);
	if (err) ERR_EXIT("Unable to allocate memory for index buffer.\nExiting...");

	err = vkMapMemory(vkData->device, vkData->indices.memory, 0, memAllocInfo.allocationSize, 0, &data);
	if (err) ERR_EXIT("Unable to map memory for index buffer.\nExiting...\n");

	memcpy(data, indices, sizeof(indices));

	vkUnmapMemory(vkData->device, vkData->indices.memory);

	err = vkBindBufferMemory(vkData->device, vkData->indices.buffer, vkData->indices.memory, 0);
	if (err) ERR_EXIT("Unable to bind mapped index memory to buffer.\nExiting...\n");*/

	vkData->vertices.vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vkData->vertices.vertexInputInfo.pNext = NULL;
	vkData->vertices.vertexInputInfo.flags = 0;
	vkData->vertices.vertexInputInfo.vertexBindingDescriptionCount = 1;
	vkData->vertices.vertexInputInfo.pVertexBindingDescriptions = vkData->vertices.vertexInputBindings;
	vkData->vertices.vertexInputInfo.vertexAttributeDescriptionCount = 2;
	vkData->vertices.vertexInputInfo.pVertexAttributeDescriptions = vkData->vertices.vertexInputAttributes;
	
	vkData->vertices.vertexInputBindings[0].binding = VERTEX_BUFFER_BIND_ID;
	vkData->vertices.vertexInputBindings[0].stride = sizeof(vertices[0]) * 6;
	vkData->vertices.vertexInputBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	vkData->vertices.vertexInputAttributes[0].location = 0;
	vkData->vertices.vertexInputAttributes[0].binding = VERTEX_BUFFER_BIND_ID;
	vkData->vertices.vertexInputAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vkData->vertices.vertexInputAttributes[0].offset = 0;

	vkData->vertices.vertexInputAttributes[1].location = 1;
	vkData->vertices.vertexInputAttributes[1].binding = VERTEX_BUFFER_BIND_ID;
	vkData->vertices.vertexInputAttributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	vkData->vertices.vertexInputAttributes[1].offset = sizeof(vertices[0]) * 3;
}

void prepareRenderPass(VulkanData *vkData)
{
	VkAttachmentDescription attachments[1] = {
		[0] = {
			.flags = 0,
			.format = vkData->format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		}
	};

	VkAttachmentReference colorReference = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkSubpassDescription subpass = {
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = NULL,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorReference,
		.pResolveAttachments = NULL,
		.pDepthStencilAttachment = NULL,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = NULL
	};

	VkRenderPassCreateInfo renderPassInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.attachmentCount = 1,
		.pAttachments = attachments,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 0,
		.pDependencies = NULL
	};

	VkResult err;
	err = vkCreateRenderPass(vkData->device, &renderPassInfo, NULL, &vkData->renderPass);
	if (err) ERR_EXIT("Unable to create render pass.\nExiting...\n");
}

void preparePipeline(VulkanData *vkData)
{
	VkShaderModule vertexShader = loadShader(vkData, "../shaders/vert.spv");
	VkShaderModule fragmentShader = loadShader(vkData, "../shaders/frag.spv");

	VkPipelineShaderStageCreateInfo shaderStages[2] = {
		[0] = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertexShader,
			.pName = "main",
			.pSpecializationInfo = NULL },
		[1] = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragmentShader,
			.pName = "main",
			.pSpecializationInfo = NULL }
	};

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE
	};

	VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.viewportCount = 1,
		.pViewports = NULL,
		.scissorCount = 1,
		.pScissors = NULL
	};

	VkPipelineRasterizationStateCreateInfo rasterState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.depthBiasConstantFactor = 0,
		.depthBiasClamp = 0,
		.depthBiasSlopeFactor = 0,
		.lineWidth = 0
	};

	VkPipelineMultisampleStateCreateInfo multisampleState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
		.minSampleShading = 0,
		.pSampleMask = NULL,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable = VK_FALSE
	};

	VkPipelineColorBlendAttachmentState colorBlendAttachments[1] = {
		[0] = {
			.blendEnable = VK_FALSE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.alphaBlendOp = VK_BLEND_OP_ADD,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT }
	};

	VkPipelineColorBlendStateCreateInfo colorBlend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_CLEAR,
		.attachmentCount = 1,
		.pAttachments = colorBlendAttachments,
		.blendConstants = {0, 0, 0, 0}
	};

	VkDynamicState dynamicStateEnables[2] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamicStateEnables
	};

	VkGraphicsPipelineCreateInfo pipeline = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.stageCount = 2,
		.pStages = shaderStages,
		.pVertexInputState = &vkData->vertices.vertexInputInfo,
		.pInputAssemblyState = &inputAssembly,
		.pTessellationState = NULL,
		.pViewportState = &viewport,
		.pRasterizationState = &rasterState,
		.pMultisampleState = &multisampleState,
		.pDepthStencilState = NULL,
		.pColorBlendState = &colorBlend,
		.pDynamicState = &dynamicState,
		.layout = NULL, //vkData->pipelineLayout,
		.renderPass = vkData->renderPass,
		.subpass = 0,
		.basePipelineHandle = 0,
		.basePipelineIndex = 0
	};

	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.initialDataSize = 0,
		.pInitialData = NULL
	};

	VkResult err;
	VkPipelineCache pipelineCache;
	err = vkCreatePipelineCache(vkData->device, &pipelineCacheCreateInfo, NULL, &pipelineCache);
	if (err) ERR_EXIT("Unable to create pipeline cache for graphics pipeline.\nExiting...\n");

	err = vkCreateGraphicsPipelines(vkData->device, pipelineCache, 1, &pipeline, NULL, &vkData->pipeline);
	if (err) ERR_EXIT("Unable to create graphics pipeline.\nExiting...\n");

	vkDestroyPipelineCache(vkData->device, pipelineCache, NULL);

	vkDestroyShaderModule(vkData->device, vertexShader, NULL);
	vkDestroyShaderModule(vkData->device, fragmentShader, NULL);
}

void prepareFramebuffers(VulkanData *vkData)
{
	VkImageView attachments[1];

	VkFramebufferCreateInfo framebufferInfo = {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.renderPass = vkData->renderPass,
		.attachmentCount = 1,
		.pAttachments = attachments,
		.width = vkData->width,
		.height = vkData->height,
		.layers = 1
	};

	vkData->framebuffers = malloc(vkData->swapchainImageCount * sizeof(VkFramebuffer));

	VkResult err;

	for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i)
	{
		attachments[0] = vkData->buffers.imageViews[i];
		err = vkCreateFramebuffer(vkData->device, &framebufferInfo, NULL, &vkData->framebuffers[i]);
		if (err) ERR_EXIT("Unable to create swapchain framebuffer.\nExiting...\n");
	}
}

void buildCommandBuffers(VulkanData *vkData)
{
	VkCommandBufferBeginInfo cmdBufferInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = NULL,
		.flags = 0,
		.pInheritanceInfo = NULL
	};

	VkClearValue clearValues[1] = {
		[0] = {
			.color = {
				.float32[0] = 0.0f,
				.float32[1] = 0.0f,
				.float32[2] = 0.0f,
				.float32[3] = 0.0f },
			.depthStencil = {
				.depth = 0.0f,
				.stencil = 0 }}
	};

	VkRenderPassBeginInfo renderPassBeginInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = NULL,
		.renderPass = vkData->renderPass,
		.renderArea = {
			.offset = {
				.x = 0,
				.y = 0 },
			.extent = {
				.width = vkData->width,
				.height = vkData->height }},
		.clearValueCount = 1,
		.pClearValues = clearValues
	};

	for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i)
	{
		VkResult err;
		renderPassBeginInfo.framebuffer = vkData->framebuffers[i];

		err = vkBeginCommandBuffer(vkData->buffers.cmdBuffers[i], &cmdBufferInfo);
		if (err) ERR_EXIT("Unable to begin swapchain image command buffer.\nExiting...\n");

		vkCmdBeginRenderPass(vkData->buffers.cmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = {
			.x = 0.0f,
			.y = 0.0f,
			.height = vkData->height,
			.width = vkData->width,
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};

		vkCmdSetViewport(vkData->buffers.cmdBuffers[i], 0, 1, &viewport);

		VkRect2D scissor = {
			.offset = {
				.x = 0,
				.y = 0 },
			.extent = {
				.width = vkData->width,
				.height = vkData->height }
		};

		vkCmdSetScissor(vkData->buffers.cmdBuffers[i], 0, 1, &scissor);

		vkCmdBindPipeline(vkData->buffers.cmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, vkData->pipeline);

		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(vkData->buffers.cmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &vkData->vertices.buffer, offsets);
		vkCmdBindIndexBuffer(vkData->buffers.cmdBuffers[i], vkData->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(vkData->buffers.cmdBuffers[i], vkData->indices.count, 1, 0, 0, 1);
		vkCmdEndRenderPass(vkData->buffers.cmdBuffers[i]);
		
		VkImageMemoryBarrier prePresentBarrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.pNext = NULL,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = vkData->buffers.images[i],
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1 }
		};

		vkCmdPipelineBarrier(vkData->buffers.cmdBuffers[i], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &prePresentBarrier);

		err = vkEndCommandBuffer(vkData->buffers.cmdBuffers[i]);
		if (err) ERR_EXIT("Error ending recording of draw command buffer.\nExiting...\n");
	}
}


void prepareVK(VulkanData *vkData)
{
	setupCommandPool(vkData);
	initSetupCommandBuffer(vkData);

	setupSwapchain(vkData);
	createCommandBuffers(vkData);
	prepareRenderPass(vkData);
	//createPipelineCache(vkData);
	prepareFramebuffers(vkData);
	//prepareDepth(vkData);
	
	prepareSemaphores(vkData);
	prepareVertices(vkData);
	//prepareDescriptorLayout(vkData);
	preparePipeline(vkData);
	//prepareDescriptorPool(vkData);
	//prepareDescriptorSet(vkData);
	buildCommandBuffers(vkData);
}

void initVK(VulkanData *vkData)
{
	//Query for required Vulkan extensions
	VkResult err;
	uint32_t requiredExtensionCount;
	const char** requiredExtensions;
	vkData->enabledExtensionCount = 0;
	
	requiredExtensions = glfwGetRequiredInstanceExtensions(&requiredExtensionCount);

	vkData->enabledExtensionCount = requiredExtensionCount;
	memcpy(vkData->enabledExtensions, requiredExtensions, sizeof(requiredExtensions[0]) * requiredExtensionCount);
	//for (uint32_t i = 0; i < requiredExtensionCount; ++i)
	//	vkData->enabledExtensions[i] = requiredExtensions[i];

	//Create Vulkan Instance
	VkApplicationInfo appInfo = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pNext = NULL,
		.pApplicationName = "Vulkan Test",
		.applicationVersion = 0,
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
		.enabledExtensionCount = vkData->enabledExtensionCount,
		.ppEnabledExtensionNames = vkData->enabledExtensions
	};

	//Creating Vulkan Instance
	err = vkCreateInstance(&instanceInfo, NULL, &vkData->instance);
	if (err) ERR_EXIT("Failed to create Vulkan instance.\nExiting...\n");

	//Enumerating physcial devices
	uint32_t physicalDeviceCount;
	err = vkEnumeratePhysicalDevices(vkData->instance, &physicalDeviceCount, NULL);
	if (err) ERR_EXIT("Failed to query the number of physical devices present.\nExiting...\n");
	if (physicalDeviceCount == 0) ERR_EXIT("No physcial devices were found with Vulkan support.\nExiting...\n");

	printf("Number of physical devices: %u\n", physicalDeviceCount);

	//Selecting the render device
	VkPhysicalDevice *physicalDevices = malloc(sizeof(VkPhysicalDevice) * physicalDeviceCount);
	err = vkEnumeratePhysicalDevices(vkData->instance, &physicalDeviceCount, physicalDevices);
	if (err) ERR_EXIT("Failed to retrieve physcial device properties.\nExiting...\n");
	
	//Just grabbing the first device present
	vkData->physicalDevice = physicalDevices[0];

	free(physicalDevices);

	vkGetPhysicalDeviceProperties(vkData->physicalDevice, &vkData->physicalDeviceProps);
	
	uint32_t deviceExtensionCount = 0;
	bool swapchainExtFound = false;
	vkData->enabledExtensionCount = 0;

	err = vkEnumerateDeviceExtensionProperties(vkData->physicalDevice, NULL, &deviceExtensionCount, NULL);
	if (err) ERR_EXIT("Unable to query device extensions.\nExiting...\n");

	if (deviceExtensionCount > 0)
	{
		VkExtensionProperties *deviceExtensionProps = malloc(deviceExtensionCount * sizeof(VkExtensionProperties));
		err = vkEnumerateDeviceExtensionProperties(vkData->physicalDevice, NULL, &deviceExtensionCount, deviceExtensionProps);
		if (err) ERR_EXIT("Unable to query device extension properties.\nExiting...\n");

		for (uint32_t i = 0; i < deviceExtensionCount && !swapchainExtFound; ++i)
		{
			if (!strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME, deviceExtensionProps[i].extensionName))
			{
				swapchainExtFound = true;
				vkData->enabledExtensions[vkData->enabledExtensionCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
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
		.flags = 0,
		.queueFamilyIndex = vkData->graphicsQueueNodeIndex,
		.queueCount = 1,
		.pQueuePriorities = queuePriorities
	};

	VkDeviceCreateInfo device = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queue,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = NULL,
		.enabledExtensionCount = vkData->enabledExtensionCount,
		.ppEnabledExtensionNames = vkData->enabledExtensions
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
	bool foundQueue = false;
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
				foundQueue = true;
			}
		}
	}

	if (presentQueueNodeIndex == UINT32_MAX)
	{
		foundQueue = false;
		for (uint32_t i = 0; i < vkData->queueCount && !foundQueue; ++i)
		{
			if (supportsPresent[i] == VK_TRUE)
			{
				presentQueueNodeIndex = i;
				foundQueue = true;
			}
		}
	}

	free(supportsPresent);

	if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX) ERR_EXIT("Could not find graphics and present queues.\nExiting...\n");
	//Possible to use separate queues for graphics and present, but this implementation doesn't incude that
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

void destroySwapchain(VulkanData *vkData)
{
	for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i)
		vkDestroyFramebuffer(vkData->device, vkData->framebuffers[i], NULL);

	vkFreeCommandBuffers(vkData->device, vkData->cmdPool, 1, &vkData->setupCmdBuffer);
	vkFreeCommandBuffers(vkData->device, vkData->cmdPool, 1, &vkData->postPresentCmdBuffer);
	vkFreeCommandBuffers(vkData->device, vkData->cmdPool, 1, &vkData->prePresentCmdBuffer);

	//vkFreeCommandBuffers(vkData->device, vkData->cmdPool, 1, &vkData->drawCmdBuffer);
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

void resizeVK(VulkanData *vkData)
{
	printf("Resizing window.\n");
	destroySwapchain(vkData);
	prepareVK(vkData);
}

void drawVK(VulkanData *vkData)
{
	vkData->fpAcquireNextImageKHR(vkData->device, vkData->swapchain, UINT64_MAX, vkData->semaphores.presentComplete, NULL, &vkData->currentBuffer);

	VkImageMemoryBarrier postPresentBarrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = NULL,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = vkData->buffers.images[vkData->currentBuffer],
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1 }
	};

	VkCommandBufferBeginInfo cmdBufferInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = NULL,
		.flags = 0,
		.pInheritanceInfo = NULL
	};

	VkResult err;

	err = vkBeginCommandBuffer(vkData->postPresentCmdBuffer, &cmdBufferInfo);
	if (err) ERR_EXIT("Unable to start post present command buffer.\nExiting...\n");

	vkCmdPipelineBarrier(vkData->postPresentCmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &postPresentBarrier);

	err = vkEndCommandBuffer(vkData->postPresentCmdBuffer);
	if (err) ERR_EXIT("Unable to end post present command buffer.\nExiting...\n");

	VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = NULL,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = NULL,
		.pWaitDstStageMask = NULL,
		.commandBufferCount = 1,
		.pCommandBuffers = &vkData->postPresentCmdBuffer,
		.signalSemaphoreCount = 0,
		.pSignalSemaphores = NULL
	};

	err = vkQueueSubmit(vkData->queue, 1, &submitInfo, VK_NULL_HANDLE);
	if (err) ERR_EXIT("Unable to submit post present command buffer to queue.\nExiting...\n");

	err = vkQueueWaitIdle(vkData->queue);
	if (err) ERR_EXIT("Error while waiting for queue to be idle.\nExiting...\n");

	VkPipelineStageFlags pipelineStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &vkData->semaphores.presentComplete;
	submitInfo.pWaitDstStageMask = &pipelineStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &vkData->buffers.cmdBuffers[vkData->currentBuffer];
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &vkData->semaphores.renderComplete;

	err = vkQueueSubmit(vkData->queue, 1, &submitInfo, VK_NULL_HANDLE);
	if (err) ERR_EXIT("Unable to submit draw command buffer to queue.\nExiting...\n");

	VkPresentInfoKHR presentInfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext = NULL,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &vkData->semaphores.renderComplete,
		.swapchainCount = 1,
		.pSwapchains = &vkData->swapchain,
		.pImageIndices = &vkData->currentBuffer,
		.pResults = NULL
	};

	err = vkData->fpQueuePresentKHR(vkData->queue, &presentInfo);
	if (err) ERR_EXIT("Unable to present swapchain image.\nExiting...\n");
}

void runWindow(Window *window)
{
	while (!glfwWindowShouldClose(window->glfwWindow))
	{
		glfwPollEvents();

		drawVK(&window->vkData);

		vkDeviceWaitIdle(window->vkData.device);
	}
}

void error_callback(int error, const char* description)
{
	printf("GLFW error code: %i\n%s\n", error, description);
	fflush(stdout);
}

void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void resize_callback(GLFWwindow *window, int width, int height)
{
	VulkanData *vkData = glfwGetWindowUserPointer(window);
	vkData->width = width;
	vkData->height = height;
	printf("Resize callback called.\n");
	resizeVK(vkData);
}

void initWindow(Window *window)
{
	memset(window, 0, sizeof(Window));

	glfwSetErrorCallback(error_callback);

	if(!glfwInit()) ERR_EXIT("Cannot initialize GLFW.\nExiting...\n");
	if(!glfwVulkanSupported()) ERR_EXIT("GLFW failed to find the Vulkan loader, do you have the most recent driver?\nExiting...\n");

	window->vkData.width = 300;
	window->vkData.height = 300;

	initVK(&window->vkData);
	
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	window->glfwWindow = glfwCreateWindow(window->vkData.width, window->vkData.height, "Vulkan Test Program", NULL, NULL);
	if (!window->glfwWindow) ERR_EXIT("Failed to create GLFW window.\nExiting...\n");

	glfwSetWindowUserPointer(window->glfwWindow, &window->vkData);
	//glfwSetWindowRefreshCallback(window->glfwWindow, );
	glfwSetFramebufferSizeCallback(window->glfwWindow, resize_callback);
	glfwSetKeyCallback(window->glfwWindow, key_callback);
	
	initSurface(&window->vkData, window->glfwWindow);

	prepareVK(&window->vkData);
}

void destroyVulkan(VulkanData *vkData)
{
	/*for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i)
		vkDestroyFramebuffer(vkData->device, vkData->framebuffers[i], NULL);
	free(vkData->framebuffers);

	//vkDestroyDescriptorPool(vkData->device, vkData->descPool, NULL);

	//if (vkData->setupCmdBuffer)
		vkFreeCommandBuffers(vkData->device, vkData->cmdPool, 1, &vkData->setupCmdBuffer);

	//vkFreeCommandBuffers(vkData->device, vkData->cmdPool, 1, &vkData->drawCmdBuffer);
	vkFreeCommandBuffers(vkData->device, vkData->cmdPool, vkData->swapchainImageCount, vkData->buffers.cmdBuffers);
	vkDestroyCommandPool(vkData->device, vkData->cmdPool, NULL);

	vkDestroyPipeline(vkData->device, vkData->pipeline, NULL);
	vkDestroyRenderPass(vkData->device, vkData->renderPass, NULL);
	//vkDestroyPipelineLayout(vkData->device, vkData->pipelineLayout, NULL);

	vkDestroyBuffer(vkData->device, vkData->vertices.buffer, NULL);
	vkFreeMemory(vkData->device, vkData->vertices.memory, NULL);

	for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i)
		vkDestroyImageView(vkData->device, vkData->buffers.imageViews[i], NULL);

	vkData->fpDestroySwapchainKHR(vkData->device, vkData->swapchain, NULL);
	free(vkData->buffers.images);
	free(vkData->buffers.cmdBuffers);
	free(vkData->buffers.imageViews);*/

	destroySwapchain(vkData);

	vkDestroyDevice(vkData->device, NULL);
	vkDestroySurfaceKHR(vkData->instance, vkData->surface, NULL);
	vkDestroyInstance(vkData->instance, NULL);

	free(vkData->queueProps);
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

	printf("Setup complete, starting main loop.\n");

	runWindow(&window);

	printf("Loop exited normally, cleaning up Vulkan structures.\n");

	destroyWindow(&window);
	return 0;
}
