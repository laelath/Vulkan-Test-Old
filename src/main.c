#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

//#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

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
	const char* enabledExtensionNames[64];

	uint32_t width;
	uint32_t height;

	VkCommandPool cmdPool;
	VkCommandBuffer drawCmdBuffer;
	VkCommandBuffer setupCmdBuffer;
	SwapchainBuffer *buffers;

	VkRenderPass renderPass;
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	struct {
		VkBuffer buffer;
		VkDeviceMemory memory;
		
		VkPipelineVertexInputStateCreateInfo vertexInputInfo;
		VkVertexInputBindingDescription vertexInputBindings[1];
		VkVertexInputAttributeDescription vertexInputAttributes[2];
	} vertices;

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

void glfw_error_callback(int error, const char* description)
{
	printf("GLFW error code: %i\n%s\n", error, description);
	fflush(stdout);
}

void glfw_key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GLFW_TRUE);
}

static void setImageLayout(VulkanData *vkData, VkImage image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout)
{
	VkResult err;

	VkImageMemoryBarrier imageMemoryBarrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = NULL,
		.srcAccessMask = 0,
		.dstAccessMask = 0,
		.oldLayout = oldImageLayout,
		.newLayout = newImageLayout,
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

	vkCmdPipelineBarrier(vkData->setupCmdBuffer, srcStages, dstStages, 0, 0, NULL, 0, NULL, 1, &imageMemoryBarrier);
}

static bool memoryTypeFromProperties(VulkanData *vkData, uint32_t typeBits, VkFlags requirementsMask, uint32_t *typeIndex)
{
	for (uint32_t i = 0; i < 32; i++)
	{
		if ((typeBits & 1) == 1)
		{
			if ((vkData->memoryProps.memoryTypes[i].propertyFlags & requirementsMask) == requirementsMask)
			{
				*typeIndex = i;
				return true;
			}
		}
		typeBits >>= 1;
	}
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
		.flags = 0,
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
			.flags = 0,
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
		};

		vkData->buffers[i].image = swapchainImages[i];
		
		setImageLayout(vkData, vkData->buffers[i].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}

	vkData->currentBuffer = 0;
	free(swapchainImages);
}

void prepareVertices(VulkanData *vkData)
{
	const float vertices[18] = {
		0.0f, 1.0f, 1.0f, 	1.0f, 0.0f, 0.0f,
		0.75f, -1.0f, 1.0f, 	0.0f, 1.0f, 0.0f,
		-0.75f, -1.0f, 1.0f, 	0.0f, 0.0f, 1.0f
	};

	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.size = sizeof(vertices),
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};

	VkMemoryAllocateInfo memAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = NULL,
		.allocationSize = 0,
		.memoryTypeIndex = 0
	};

	VkMemoryRequirements memoryRequirements;
	VkResult err;
	void *data;

	err = vkCreateBuffer(vkData->device, &bufferInfo, NULL, &vkData->vertices.buffer);
	if (err) ERR_EXIT("Unable to create vertex buffer.\nExiting...\n");

	vkGetBufferMemoryRequirements(vkData->device, vkData->vertices.buffer, &memoryRequirements);

	memAllocInfo.allocationSize = memoryRequirements.size;
	if (!memoryTypeFromProperties(vkData, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &memAllocInfo.memoryTypeIndex))
		ERR_EXIT("Unable to find suitable memory type for vertex buffer.\nExiting...\n");

	err = vkAllocateMemory(vkData->device, &memAllocInfo, NULL, &vkData->vertices.memory);
	if (err) ERR_EXIT("Unable to allocate memory for vertex buffer.\nExiting...\n");

	err = vkMapMemory(vkData->device, vkData->vertices.memory, 0, memAllocInfo.allocationSize, 0, &data);
	if (err) ERR_EXIT("Unable to map memory for vertex buffer.\nExiting...\n");

	memcpy(data, vertices, sizeof(vertices));

	vkUnmapMemory(vkData->device, vkData->vertices.memory);
	
	err = vkBindBufferMemory(vkData->device, vkData->vertices.buffer, vkData->vertices.memory, 0);
	if (err) ERR_EXIT("Unable to bind mapped vertex memory to buffer.\nExiting...\n");

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

	vkData->vertices.vertexInputAttributes[0].binding = VERTEX_BUFFER_BIND_ID;
	vkData->vertices.vertexInputAttributes[0].location = 0;
	vkData->vertices.vertexInputAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vkData->vertices.vertexInputAttributes[0].offset = 0;

	vkData->vertices.vertexInputAttributes[1].binding = VERTEX_BUFFER_BIND_ID;
	vkData->vertices.vertexInputAttributes[1].location = 1;
	vkData->vertices.vertexInputAttributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	vkData->vertices.vertexInputAttributes[1].offset = sizeof(vertices[0]) * 3;
}

void prepareRenderPass(VulkanData *vkData)
{
	VkAttachmentDescription attachments[1] = {
		[0] = {
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
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.flags = 0,
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
	VkResult err;

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
		.layout = vkData->pipelineLayout
	};

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};

	VkPipelineRasterizationStateCreateInfo rasterState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.depthBiasEnable = VK_FALSE
	};

	VkPipelineColorBlendAttachmentState colorBlendAttachments[1] = {
		[0] = {
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT,
			.blendEnable = VK_FALSE
		}
	};

	VkPipelineColorBlendStateCreateInfo colorBlend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.attachmentCount = 1,
		.pAttachments = colorBlendAttachments
	};

	VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.viewportCount = 1,
		.scissorCount = 1
	};

	VkPipelineMultisampleStateCreateInfo multisampleState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
		.pSampleMask = NULL,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable = VK_FALSE
	};

	VkPipelineShaderStageCreateInfo shaderStages[2] = {
		[0] = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			//.module = prepareVertexShader(vkData),
			.pName = "main" },
		[1] = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			//.module = prepareFragmentShader(vkData),
			.pName = "main" }
	};
}

void prepareSwapchain(VulkanData *vkData)
{
	VkResult err;
	
	//Create command pool
	VkCommandPoolCreateInfo cmdPoolCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.queueFamilyIndex = vkData->graphicsQueueNodeIndex,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
	};

	err = vkCreateCommandPool(vkData->device, &cmdPoolCreateInfo, NULL, &vkData->cmdPool);
	if (err) ERR_EXIT("Failed to create command pool.\nExiting...\n");

	//Create draw command buffer
	VkCommandBufferAllocateInfo cmdBufferAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = NULL,
		.commandPool = vkData->cmdPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	err = vkAllocateCommandBuffers(vkData->device, &cmdBufferAllocInfo, &vkData->drawCmdBuffer);
	if (err) ERR_EXIT("Failed to allocate draw command buffer.\nExiting...\n");

	//Create setup command buffer
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

	prepareBuffers(vkData);
	//prepareDepth(vkData);
	prepareVertices(vkData);
	//prepareDescriptorLayout(vkData);
	prepareRenderPass(vkData);
	preparePipeline(vkData);

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
		.enabledExtensionCount = vkData->enabledExtensionCount,
		.ppEnabledExtensionNames = vkData->enabledExtensionNames
	};

	//Creating Vulkan Instance
	err = vkCreateInstance(&instanceInfo, NULL, &vkData->instance);
	if (err) ERR_EXIT("Failed to create Vulkan instance.\nExiting...\n");

	//Enumerating physcial devices
	uint32_t physicalDeviceCount;
	err = vkEnumeratePhysicalDevices(vkData->instance, &physicalDeviceCount, NULL);
	if (err) ERR_EXIT("Failed to query the number of physical devices present.\nExiting...\n");
	if (physicalDeviceCount == 0) ERR_EXIT("No physcial devices were found with Vulkan support.\nExiting...\n");
	
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
	glfwSetKeyCallback(window->glfwWindow, glfw_key_callback);
	
	initSurface(&window->vkData, window->glfwWindow);

	prepareSwapchain(&window->vkData);
}

void destroyVulkan(VulkanData *vkData)
{
	//for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i)
	//	vkDestroyFramebuffer(vkData->device, vkData->framebuffers[i], NULL);
	//free(vkData->framebuffers);

	//vkDestroyDescriptorPool(vkData->device, vkData->descPool, NULL);

	//if (vkData->setupCmdBuffer)
		vkFreeCommandBuffers(vkData->device, vkData->cmdPool, 1, &vkData->setupCmdBuffer);

	vkFreeCommandBuffers(vkData->device, vkData->cmdPool, 1, &vkData->drawCmdBuffer);
	vkDestroyCommandPool(vkData->device, vkData->cmdPool, NULL);

	//vkDestroyPipeline(vkData->device, vkData->pipeline, NULL);
	vkDestroyRenderPass(vkData->device, vkData->renderPass, NULL);
	//vkDestroyPipelineLayout(vkData->device, vkData->pipelineLayout, NULL);

	vkDestroyBuffer(vkData->device, vkData->vertices.buffer, NULL);
	vkFreeMemory(vkData->device, vkData->vertices.memory, NULL);

	//for (uint32_t i = 0; i < vkData->swapchainImageCount; ++i)
		//vkDestroyImageView(vkData->device, vkData->buffers[i].view, NULL);

	vkData->fpDestroySwapchainKHR(vkData->device, vkData->swapchain, NULL);
	free(vkData->buffers);

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

	printf("Setup complete, destroying Vulkan structures.\n");

	destroyWindow(&window);
	return 0;
}
