/*! \file triangleApplication.cpp
	\brief Contains the function definitions for creating a Vulkan Renderer that displays a triangle
	\date 02/06/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#include "triangleApplication.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stb_image.h>
#include <string>
#include <tiny_obj_loader.h>
#include <unordered_map>
#include <vector>

#include "vulkan/vulkan.hpp"

#include "attributeMacros.h"
#include "constants.h"
#include "typedefs.h"
#include "vertex.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan_raii.hpp>

void VulkanApplication::run()
{
	initWindow();
	initVulkan();
	mainLoop();
	cleanup();
}

void VulkanApplication::initWindow()
{
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	mWindow = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>(glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr),
																		glfwDestroyWindow);

	glfwSetWindowUserPointer(mWindow.get(), this);
	glfwSetFramebufferSizeCallback(mWindow.get(), framebufferResizeCallback);
}

void VulkanApplication::initVulkan()
{
	createInstance();
	setupDebugMessenger();
	createSurface();
	pickPhysicalDevice();
	createLogicalDevice();
	createSwapChain();
	createImageViews();
	createDescriptorSetLayout();
	createGraphicsPipeline();
	createCommandPool();
	createDepthResources();
	createTextureImage();
	createTextureImageView();
	createTextureSampler();
	loadModel();
	createVertexBuffer();
	createIndexBuffer();
	createUniformBuffers();
	createDescriptorPool();
	createDescriptorSets();
	createCommandBuffers();
	createSyncObjects();
}

void VulkanApplication::createInstance()
{
	constexpr vk::ApplicationInfo appInfo{.pApplicationName = "Hello Triangle",
										  .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
										  .pEngineName = "No Engine",
										  .engineVersion = VK_MAKE_VERSION(1, 0, 0),
										  .apiVersion = vk::ApiVersion14};

	// Get the required layers
	std::vector<const char *> requiredLayers;

	if (enableValidationLayers)
	{
		requiredLayers.assign(validationLayers.begin(), validationLayers.end());
	}

	// Check if the required layers are supported by the Vulkan implementation
	const std::vector<vk::LayerProperties> layerProperties = mContext.enumerateInstanceLayerProperties();
	for (const auto &requiredLayer : requiredLayers)
	{
		if (std::ranges::none_of(layerProperties, [requiredLayer](const auto &layerProperty) noexcept {
				return strcmp(layerProperty.layerName, requiredLayer) == 0;
			}))
		{
			throw std::runtime_error("Required layer not supported: " + std::string(requiredLayer));
		}
	}

	// Get the required extensions
	const std::vector<const char *> requiredExtensions = getRequiredExtensions();

	// Check if the required GLFW extensions are supported by the Vulkan implementation
	const std::vector<vk::ExtensionProperties> extensionProperties = mContext.enumerateInstanceExtensionProperties();
	for (const auto &requiredExtension : requiredExtensions)
	{
		if (std::ranges::none_of(extensionProperties, [requiredExtension](const vk::ExtensionProperties &extension) noexcept {
				return strcmp(extension.extensionName, requiredExtension) == 0;
			}))
		{
			throw std::runtime_error("Required extension not supported: " + std::string(requiredExtension));
		}
	}

	const vk::InstanceCreateInfo createInfo{
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = sc<ui>(requiredLayers.size()),
		.ppEnabledLayerNames = requiredLayers.data(),
		.enabledExtensionCount = sc<ui>(requiredExtensions.size()),
		.ppEnabledExtensionNames = requiredExtensions.data(),
	};

	mInstance = vk::raii::Instance(mContext, createInfo);
}

void VulkanApplication::setupDebugMessenger()
{
	if (!enableValidationLayers)
	{
		return;
	}

	constexpr vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose
																  | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
																  | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
	constexpr vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
																 | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance
																 | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

	constexpr vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{
		.messageSeverity = severityFlags, .messageType = messageTypeFlags, .pfnUserCallback = &debugCallback};

	mDebugMessenger = mInstance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
}

void VulkanApplication::createSurface()
{
	VkSurfaceKHR surface{};

	if (glfwCreateWindowSurface(*mInstance, mWindow.get(), nullptr, &surface) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create window surface!");
	}

	mSurface = vk::raii::SurfaceKHR(mInstance, surface);
}

void VulkanApplication::pickPhysicalDevice()
{
	const auto devices{mInstance.enumeratePhysicalDevices()};

	const auto devIter = std::ranges::find_if(devices, [&](const vk::raii::PhysicalDevice &physicalDevice) {
		// Check if the device supports the Vulkan 1.3 API version
		const bool supportsVulkan1_3{physicalDevice.getProperties().apiVersion >= VK_API_VERSION_1_3};

		// Check if any of the queue families support graphics operations
		std::vector<vk::QueueFamilyProperties> queueFamilies{physicalDevice.getQueueFamilyProperties()};

		const bool supportsGraphics{std::ranges::any_of(queueFamilies, [](const vk::QueueFamilyProperties &qfp) noexcept {
			return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
		})};

		// Check if all required device extensions are available
		std::vector<vk::ExtensionProperties> availableDeviceExtensions{physicalDevice.enumerateDeviceExtensionProperties()};

		const bool supportsAllRequiredExtensions{
			std::ranges::all_of(requiredDeviceExtensions, [&availableDeviceExtensions](const auto &requiredDeviceExtension) {
				return std::ranges::any_of(availableDeviceExtensions,
										   [requiredDeviceExtension](const vk::ExtensionProperties &availableDeviceExtension) noexcept {
											   return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0;
										   });
			})};

		auto features{physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features,
														   vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()};
		const bool supportsRequiredFeatures{
			features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy
			&& features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering
			&& features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState};

		return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
	});

	if (devIter != devices.end())
	{
		mPhysicalDevice = *devIter;
	}
	else
	{
		throw std::runtime_error("failed to find a suitable GPU!");
	}
}

void VulkanApplication::createLogicalDevice()
{
	// Find the index of the first queue family that supports graphics
	const std::vector<vk::QueueFamilyProperties> queueFamilyProperties{mPhysicalDevice.getQueueFamilyProperties()};

	// Get the first index into queueFamilyProperties which supports both graphics and present
	for (ui qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
	{
		if ((queueFamilyProperties.at(qfpIndex).queueFlags & vk::QueueFlagBits::eGraphics)
			&& mPhysicalDevice.getSurfaceSupportKHR(qfpIndex, *mSurface) == VK_TRUE)
		{
			// found a queue family that supports both graphics and present
			mQueueIndex = sc<si>(qfpIndex);
			break;
		}
	}
	if (mQueueIndex == -1)
	{
		throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
	}

	// Query for Vulkan 1.3 features
	const vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features,
							 vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
		featureChain{
			{.features = {.samplerAnisotropy = VK_TRUE}},				// Enable samplerAnisotropy
			{.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE}, // Enable synchronization and dynamic rendering from Vulkan 1.3
			{.extendedDynamicState = VK_TRUE}							// Enable extended dynamic state from the extension
		};

	// Create a device
	constexpr float queuePriority{0.5F};
	const vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
		.queueFamilyIndex = sc<ui>(mQueueIndex), .queueCount = 1, .pQueuePriorities = &queuePriority};

	const vk::DeviceCreateInfo deviceCreateInfo{.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
												.queueCreateInfoCount = 1,
												.pQueueCreateInfos = &deviceQueueCreateInfo,
												.enabledExtensionCount = sc<ui>(requiredDeviceExtensions.size()),
												.ppEnabledExtensionNames = requiredDeviceExtensions.data()};

	mDevice = vk::raii::Device(mPhysicalDevice, deviceCreateInfo);
	mPresentQueue = vk::raii::Queue(mDevice, sc<ui>(mQueueIndex), 0);
}

void VulkanApplication::createSwapChain()
{
	const vk::SurfaceCapabilitiesKHR surfaceCapabilities{mPhysicalDevice.getSurfaceCapabilitiesKHR(mSurface)};

	mSwapChainExtent = chooseSwapExtent(surfaceCapabilities);
	mSwapChainSurfaceFormat = chooseSwapSurfaceFormat(mPhysicalDevice.getSurfaceFormatsKHR(*mSurface));

	const vk::SwapchainCreateInfoKHR swapChainCreateInfo{.surface = *mSurface,
														 .minImageCount = chooseSwapMinImageCount(surfaceCapabilities),
														 .imageFormat = mSwapChainSurfaceFormat.format,
														 .imageColorSpace = mSwapChainSurfaceFormat.colorSpace,
														 .imageExtent = mSwapChainExtent,
														 .imageArrayLayers = 1,
														 .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
														 .imageSharingMode = vk::SharingMode::eExclusive,
														 .preTransform = surfaceCapabilities.currentTransform,
														 .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
														 .presentMode
														 = chooseSwapPresentMode(mPhysicalDevice.getSurfacePresentModesKHR(*mSurface)),
														 .clipped = VK_TRUE};

	mSwapChain = vk::raii::SwapchainKHR(mDevice, swapChainCreateInfo);
	mSwapChainImages = mSwapChain.getImages();
}

void VulkanApplication::createImageViews()
{
	assert(mSwapChainImageViews.empty());

	vk::ImageViewCreateInfo imageViewCreateInfo{
		.viewType = vk::ImageViewType::e2D,
		.format = mSwapChainSurfaceFormat.format,
		.subresourceRange
		= {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}};

	for (const vk::Image &image : mSwapChainImages)
	{
		imageViewCreateInfo.image = image;
		mSwapChainImageViews.emplace_back(mDevice, imageViewCreateInfo);
	}
}

void VulkanApplication::createDescriptorSetLayout()
{
	const std::array<vk::DescriptorSetLayoutBinding, 2> bindings
		= {vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr),
		   vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr)};

	const vk::DescriptorSetLayoutCreateInfo layoutInfo{.bindingCount = sc<ui>(bindings.size()), .pBindings = bindings.data()};
	mDescriptorSetLayout = vk::raii::DescriptorSetLayout(mDevice, layoutInfo);
}

void VulkanApplication::createGraphicsPipeline()
{
	const vk::raii::ShaderModule shaderModule{createShaderModule(readFile("resources/shaders/triangle.spv"))};

	const vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
		.stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule, .pName = "vertMain"};
	const vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
		.stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "fragMain"};

	ATTR_MAYBE_UNUSED const std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{vertShaderStageInfo, fragShaderStageInfo};

	const vk::VertexInputBindingDescription bindingDescription{Vertex::getBindingDescription()};
	const std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions{Vertex::getAttributeDescriptions()};
	const vk::PipelineVertexInputStateCreateInfo vertexInputInfo{.vertexBindingDescriptionCount = 1,
																 .pVertexBindingDescriptions = &bindingDescription,
																 .vertexAttributeDescriptionCount = sc<ui>(attributeDescriptions.size()),
																 .pVertexAttributeDescriptions = attributeDescriptions.data()};
	const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = vk::PrimitiveTopology::eTriangleList};
	const vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1, .scissorCount = 1};

	const vk::PipelineRasterizationStateCreateInfo rasterizer{.depthClampEnable = vk::False,
															  .rasterizerDiscardEnable = vk::False,
															  .polygonMode = vk::PolygonMode::eFill,
															  .cullMode = vk::CullModeFlagBits::eBack,
															  .frontFace = vk::FrontFace::eCounterClockwise,
															  .depthBiasEnable = vk::False,
															  .depthBiasSlopeFactor = 1.0F,
															  .lineWidth = 1.0F};

	const vk::PipelineMultisampleStateCreateInfo multisampling{.rasterizationSamples = vk::SampleCountFlagBits::e1,
															   .sampleShadingEnable = vk::False};

	const vk::PipelineDepthStencilStateCreateInfo depthStencil{.depthTestEnable = vk::True,
															   .depthWriteEnable = vk::True,
															   .depthCompareOp = vk::CompareOp::eLess,
															   .depthBoundsTestEnable = vk::False,
															   .stencilTestEnable = vk::False};

	const vk::PipelineColorBlendAttachmentState colorBlendAttachment{.blendEnable = vk::False,
																	 .colorWriteMask
																	 = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
																	 | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

	const vk::PipelineColorBlendStateCreateInfo colorBlending{
		.logicOpEnable = vk::False, .logicOp = vk::LogicOp::eCopy, .attachmentCount = 1, .pAttachments = &colorBlendAttachment};

	const std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
	const vk::PipelineDynamicStateCreateInfo dynamicState{.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
														  .pDynamicStates = dynamicStates.data()};

	const vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
		.setLayoutCount = 1, .pSetLayouts = &*mDescriptorSetLayout, .pushConstantRangeCount = 0};

	mPipelineLayout = vk::raii::PipelineLayout(mDevice, pipelineLayoutInfo);

	const vk::Format depthFormat{findDepthFormat()};

	vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain
		= {{.stageCount = 2,
			.pStages = shaderStages.data(),
			.pVertexInputState = &vertexInputInfo,
			.pInputAssemblyState = &inputAssembly,
			.pViewportState = &viewportState,
			.pRasterizationState = &rasterizer,
			.pMultisampleState = &multisampling,
			.pDepthStencilState = &depthStencil,
			.pColorBlendState = &colorBlending,
			.pDynamicState = &dynamicState,
			.layout = mPipelineLayout,
			.renderPass = nullptr},
		   {.colorAttachmentCount = 1, .pColorAttachmentFormats = &mSwapChainSurfaceFormat.format, .depthAttachmentFormat = depthFormat}};

	mGraphicsPipeline = vk::raii::Pipeline(mDevice, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

void VulkanApplication::createCommandPool()
{
	const vk::CommandPoolCreateInfo poolInfo{.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
											 .queueFamilyIndex = sc<ui>(mQueueIndex)};

	mCommandPool = vk::raii::CommandPool(mDevice, poolInfo);
}

void VulkanApplication::createDepthResources()
{
	const vk::Format depthFormat{findDepthFormat()};

	createImage(mSwapChainExtent.width, mSwapChainExtent.height, 1, depthFormat, vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal, mDepthImage, mDepthImageMemory);

	mDepthImageView = createImageView(mDepthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
}

void VulkanApplication::createTextureImage()
{
	si texWidth{};
	si texHeight{};
	si texChannels{};

	stbi_uc *pixels{stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha)};

	const vk::DeviceSize imageSize{sc<vk::DeviceSize>(texWidth * texHeight * 4)};

	mMipLevels = sc<ui>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

	if (pixels == nullptr)
	{
		throw std::runtime_error("Failed to load texture image!");
	}

	vk::raii::Buffer stagingBuffer({});
	vk::raii::DeviceMemory stagingBufferMemory({});
	createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc,
				 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

	void *data = stagingBufferMemory.mapMemory(0, imageSize);
	memcpy(data, pixels, imageSize);
	stagingBufferMemory.unmapMemory();

	stbi_image_free(pixels);

	createImage(sc<ui>(texWidth), sc<ui>(texHeight), mMipLevels, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
				vk::MemoryPropertyFlagBits::eDeviceLocal, mTextureImage, mTextureImageMemory);

	transitionImageLayout(mTextureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mMipLevels);
	copyBufferToImage(stagingBuffer, mTextureImage, sc<ui>(texWidth), sc<ui>(texHeight));

	generateMipmaps(mTextureImage, vk::Format::eR8G8B8A8Srgb, sc<ui>(texWidth), sc<ui>(texHeight), mMipLevels);
}

void VulkanApplication::createTextureImageView()
{
	mTextureImageView = createImageView(mTextureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, mMipLevels);
}

void VulkanApplication::createTextureSampler()
{
	const vk::PhysicalDeviceProperties properties = mPhysicalDevice.getProperties();
	const vk::SamplerCreateInfo samplerInfo{.magFilter = vk::Filter::eLinear,
											.minFilter = vk::Filter::eLinear,
											.mipmapMode = vk::SamplerMipmapMode::eLinear,
											.addressModeU = vk::SamplerAddressMode::eRepeat,
											.addressModeV = vk::SamplerAddressMode::eRepeat,
											.addressModeW = vk::SamplerAddressMode::eRepeat,
											.mipLodBias = 0.0F,
											.anisotropyEnable = vk::True,
											.maxAnisotropy = properties.limits.maxSamplerAnisotropy,
											.compareEnable = vk::False,
											.compareOp = vk::CompareOp::eAlways,
											.minLod = 0.0F,
											.maxLod = vk::LodClampNone};

	mTextureSampler = vk::raii::Sampler(mDevice, samplerInfo);
}

void VulkanApplication::loadModel()
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn;
	std::string err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, MODEL_PATH.c_str()))
	{
		throw std::runtime_error(warn + err);
	}

	std::unordered_map<Vertex, ui> uniqueVertices{};

	for (const tinyobj::shape_t &shape : shapes)
	{
		for (const tinyobj::index_t &index : shape.mesh.indices)
		{
			Vertex vertex{};

			vertex.pos = {attrib.vertices.at(sc<std::size_t>((3 * index.vertex_index) + 0)),
						  attrib.vertices.at(sc<std::size_t>((3 * index.vertex_index) + 1)),
						  attrib.vertices.at(sc<std::size_t>((3 * index.vertex_index) + 2))};

			vertex.texCoord = {attrib.texcoords.at(sc<std::size_t>((2 * index.texcoord_index) + 0)),
							   1.0F - attrib.texcoords.at(sc<std::size_t>((2 * index.texcoord_index) + 1))};

			vertex.color = {1.0F, 1.0F, 1.0F};

			if (!uniqueVertices.contains(vertex))
			{
				uniqueVertices[vertex] = sc<ui>(mVertices.size());
				mVertices.push_back(vertex);
			}

			mIndices.push_back(uniqueVertices[vertex]);
		}
	}
}

void VulkanApplication::createVertexBuffer()
{
	const vk::DeviceSize bufferSize{sizeof(mVertices.at(0)) * mVertices.size()};
	vk::raii::Buffer stagingBuffer({});
	vk::raii::DeviceMemory stagingBufferMemory({});

	createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
				 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

	void *dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
	memcpy(dataStaging, mVertices.data(), bufferSize);
	stagingBufferMemory.unmapMemory();

	createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
				 vk::MemoryPropertyFlagBits::eDeviceLocal, mVertexBuffer, mVertexBufferMemory);

	copyBuffer(stagingBuffer, mVertexBuffer, bufferSize);
}

void VulkanApplication::createIndexBuffer()
{
	const vk::DeviceSize bufferSize{sizeof(mIndices.at(0)) * mIndices.size()};

	vk::raii::Buffer stagingBuffer({});
	vk::raii::DeviceMemory stagingBufferMemory({});

	createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
				 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

	void *dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
	memcpy(dataStaging, mIndices.data(), bufferSize);
	stagingBufferMemory.unmapMemory();

	createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
				 vk::MemoryPropertyFlagBits::eDeviceLocal, mIndexBuffer, mIndexBufferMemory);

	copyBuffer(stagingBuffer, mIndexBuffer, bufferSize);
}

void VulkanApplication::createUniformBuffers()
{
	mUniformBuffers.clear();
	mUniformBuffersMemory.clear();
	mUniformBuffersMapped.clear();

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		const vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
		vk::raii::Buffer buffer({});
		vk::raii::DeviceMemory bufferMem({});
		createBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer,
					 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, buffer, bufferMem);

		mUniformBuffers.emplace_back(std::move(buffer));
		mUniformBuffersMemory.emplace_back(std::move(bufferMem));
		mUniformBuffersMapped.emplace_back(mUniformBuffersMemory.at(i).mapMemory(0, bufferSize));
	}
}

void VulkanApplication::createDescriptorPool()
{
	std::array<vk::DescriptorPoolSize, 2> poolSize{vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT),
												   vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT)};

	const vk::DescriptorPoolCreateInfo poolInfo{.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
												.maxSets = MAX_FRAMES_IN_FLIGHT,
												.poolSizeCount = sc<ui>(poolSize.size()),
												.pPoolSizes = poolSize.data()};

	mDescriptorPool = vk::raii::DescriptorPool(mDevice, poolInfo);
}

void VulkanApplication::createDescriptorSets()
{
	std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *mDescriptorSetLayout);
	const vk::DescriptorSetAllocateInfo allocInfo{
		.descriptorPool = mDescriptorPool, .descriptorSetCount = sc<ui>(layouts.size()), .pSetLayouts = layouts.data()};

	mDescriptorSets = mDevice.allocateDescriptorSets(allocInfo);

	mDescriptorSets.clear();
	mDescriptorSets = mDevice.allocateDescriptorSets(allocInfo);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		const vk::DescriptorBufferInfo bufferInfo{.buffer = mUniformBuffers.at(i), .offset = 0, .range = sizeof(UniformBufferObject)};
		const vk::DescriptorImageInfo imageInfo{
			.sampler = mTextureSampler, .imageView = mTextureImageView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};

		const std::array<vk::WriteDescriptorSet, 2> descriptorWrites{
			vk::WriteDescriptorSet{.dstSet = mDescriptorSets.at(i),
								   .dstBinding = 0,
								   .dstArrayElement = 0,
								   .descriptorCount = 1,
								   .descriptorType = vk::DescriptorType::eUniformBuffer,
								   .pBufferInfo = &bufferInfo},
			vk::WriteDescriptorSet{.dstSet = mDescriptorSets.at(i),
								   .dstBinding = 1,
								   .dstArrayElement = 0,
								   .descriptorCount = 1,
								   .descriptorType = vk::DescriptorType::eCombinedImageSampler,
								   .pImageInfo = &imageInfo}};

		mDevice.updateDescriptorSets(descriptorWrites, {});
	}
}

void VulkanApplication::createCommandBuffers()
{
	mCommandBuffers.clear();
	const vk::CommandBufferAllocateInfo allocInfo{
		.commandPool = mCommandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = MAX_FRAMES_IN_FLIGHT};

	mCommandBuffers = std::move(vk::raii::CommandBuffers(mDevice, allocInfo));
}

void VulkanApplication::createSyncObjects()
{
	assert(mPresentCompleteSemaphores.empty() && mRenderCompleteSemaphores.empty() && mInFlightFences.empty());

	for (size_t i = 0; i < mSwapChainImages.size(); i++)
	{
		mRenderCompleteSemaphores.emplace_back(mDevice, vk::SemaphoreCreateInfo());
	}

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		mPresentCompleteSemaphores.emplace_back(mDevice, vk::SemaphoreCreateInfo());
		mInFlightFences.emplace_back(mDevice, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
	}
}

vk::Extent2D VulkanApplication::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities)
{
	if (capabilities.currentExtent.width != std::numeric_limits<ui>::max())
	{
		return capabilities.currentExtent;
	}

	si width{};
	si height{};
	glfwGetFramebufferSize(mWindow.get(), &width, &height);

	return {.width = std::clamp<ui>(sc<ui>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			.height = std::clamp<ui>(sc<ui>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
}

ATTR_NODISCARD vk::raii::ShaderModule VulkanApplication::createShaderModule(const std::vector<char> &code) const
{
	const vk::ShaderModuleCreateInfo createInfo{.codeSize = code.size() * sizeof(char), .pCode = reinterpret_cast<const ui *>(code.data())};

	vk::raii::ShaderModule shaderModule{mDevice, createInfo};

	return shaderModule;
}

void VulkanApplication::recordCommandBuffer(ui imageIndex)
{
	const vk::raii::CommandBuffer &commandBuffer = mCommandBuffers.at(mFrameIndex);
	commandBuffer.begin({});

	// Before starting rendering, transition the swapchain image to COLOR_ATTACHMENT_OPTIMALT
	transition_image_layout(mSwapChainImages.at(imageIndex), vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
							{},													// srcAccessMask (no need to wait for previous operations)
							vk::AccessFlagBits2::eColorAttachmentWrite,			// dstAccessMask
							vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
							vk::PipelineStageFlagBits2::eColorAttachmentOutput, // dstStage
							vk::ImageAspectFlagBits::eColor);

	// Transition depth image to depth attachment optimal layout
	transition_image_layout(*mDepthImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal,
							vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
							vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
							vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
							vk::ImageAspectFlagBits::eDepth);

	const vk::ClearValue clearColor{vk::ClearColorValue(std::array<float, 4>{0.0F, 0.0F, 0.0F, 1.0F})};
	const vk::ClearValue clearDepth{vk::ClearDepthStencilValue(1.0F, 0)};
	const vk::RenderingAttachmentInfo colorAttachmentInfo{
		.imageView = mSwapChainImageViews.at(imageIndex),
		.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
		.loadOp = vk::AttachmentLoadOp::eClear,
		.storeOp = vk::AttachmentStoreOp::eStore,
		.clearValue = clearColor,
	};

	const vk::RenderingAttachmentInfo depthAttachmentInfo{
		.imageView = mDepthImageView,
		.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
		.loadOp = vk::AttachmentLoadOp::eClear,
		.storeOp = vk::AttachmentStoreOp::eDontCare,
		.clearValue = clearDepth,
	};

	const vk::RenderingInfo renderingInfo = {.renderArea = {.offset = {.x = 0, .y = 0}, .extent = mSwapChainExtent},
											 .layerCount = 1,
											 .colorAttachmentCount = 1,
											 .pColorAttachments = &colorAttachmentInfo,
											 .pDepthAttachment = &depthAttachmentInfo};

	commandBuffer.beginRendering(renderingInfo);
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *mGraphicsPipeline);
	commandBuffer.setViewport(0, vk::Viewport{.x = 0.0F,
											  .y = 0.0F,
											  .width = sc<float>(mSwapChainExtent.width),
											  .height = sc<float>(mSwapChainExtent.height),
											  .minDepth = 0.0F,
											  .maxDepth = 1.0F});
	commandBuffer.setScissor(0, vk::Rect2D{.offset = {.x = 0, .y = 0}, .extent = mSwapChainExtent});
	commandBuffer.bindVertexBuffers(0, *mVertexBuffer, {0});
	commandBuffer.bindIndexBuffer(*mIndexBuffer, 0, vk::IndexType::eUint32);
	commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mPipelineLayout, 0, *mDescriptorSets.at(mFrameIndex), nullptr);
	commandBuffer.drawIndexed(sc<ui>(mIndices.size()), 1, 0, 0, 0);
	commandBuffer.endRendering();

	// After rendering, transition the swapchain image to PRESENT_SRC
	transition_image_layout(mSwapChainImages.at(imageIndex), vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
							vk::AccessFlagBits2::eColorAttachmentWrite,			// srcAccessMask
							{},													// dstAccessMask
							vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
							vk::PipelineStageFlagBits2::eBottomOfPipe,			// dstStage
							vk::ImageAspectFlagBits::eColor);

	commandBuffer.end();
}

void VulkanApplication::transition_image_layout(vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
												vk::AccessFlags2 srcAccessMask, vk::AccessFlags2 dstAccessMask,
												vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask,
												vk::ImageAspectFlags aspectFlags)
{
	const vk::ImageMemoryBarrier2 barrier{
		.srcStageMask = srcStageMask,
		.srcAccessMask = srcAccessMask,
		.dstStageMask = dstStageMask,
		.dstAccessMask = dstAccessMask,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {.aspectMask = aspectFlags, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
	};

	const vk::DependencyInfo dependencyInfo{
		.dependencyFlags = {},
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &barrier,
	};

	mCommandBuffers.at(mFrameIndex).pipelineBarrier2(dependencyInfo);
}

void VulkanApplication::recreateSwapChain()
{
	si width{0};
	si height{0};
	glfwGetFramebufferSize(mWindow.get(), &width, &height);

	while (width == 0 || height == 0)
	{
		glfwGetFramebufferSize(mWindow.get(), &width, &height);
		glfwWaitEvents();
	}

	mDevice.waitIdle();

	cleanupSwapChain();

	createSwapChain();
	createImageViews();
	createDepthResources();
}

void VulkanApplication::cleanupSwapChain()
{
	mSwapChainImageViews.clear();
	mSwapChain = nullptr;
}

ATTR_NODISCARD ui VulkanApplication::findMemoryType(const ui typeFilter, vk::MemoryPropertyFlags properties) const
{
	vk::PhysicalDeviceMemoryProperties memProperties{mPhysicalDevice.getMemoryProperties()};

	for (ui i = 0; i < memProperties.memoryTypeCount; i++)
	{
		if ((typeFilter & (1U << i)) == 1 && (memProperties.memoryTypes.at(i).propertyFlags & properties) == properties)
		{
			return i;
		}
	}

	throw std::runtime_error("Failed to find suitable memory type!");
}

void VulkanApplication::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties,
									 vk::raii::Buffer &buffer, vk::raii::DeviceMemory &bufferMemory)
{
	const vk::BufferCreateInfo bufferInfo{.size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive};
	buffer = vk::raii::Buffer(mDevice, bufferInfo);

	const vk::MemoryRequirements memoryRequirements{buffer.getMemoryRequirements()};
	const vk::MemoryAllocateInfo memoryAllocateInfo{.allocationSize = memoryRequirements.size,
													.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties)};

	bufferMemory = vk::raii::DeviceMemory(mDevice, memoryAllocateInfo);
	buffer.bindMemory(*bufferMemory, 0);
}

void VulkanApplication::copyBuffer(vk::raii::Buffer &srcBuffer, vk::raii::Buffer &dstBuffer, vk::DeviceSize size)
{
	const vk::CommandBufferAllocateInfo allocInfo{
		.commandPool = mCommandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1};

	const vk::raii::CommandBuffer commandCopyBuffer{std::move(mDevice.allocateCommandBuffers(allocInfo).front())};

	commandCopyBuffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
	commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy(0, 0, size));
	commandCopyBuffer.end();

	mPresentQueue.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandCopyBuffer}, nullptr);
	mPresentQueue.waitIdle();
}

void VulkanApplication::updateUniformBuffer(ui currentImage)
{
	static auto startTime = std::chrono::high_resolution_clock::now();

	auto currentTime = std::chrono::high_resolution_clock::now();
	const float time = std::chrono::duration<float>(currentTime - startTime).count();

	UniformBufferObject ubo{};
	ubo.model = rotate(glm::mat4(1.0F), time * glm::radians(90.0F), glm::vec3(0.0F, 0.0F, 1.0F));
	ubo.view = lookAt(glm::vec3(2.0F, 2.0F, 2.0F), glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.0F, 0.0F, 1.0F));
	ubo.proj = glm::perspective(glm::radians(45.0F), sc<float>(mSwapChainExtent.width) / sc<float>(mSwapChainExtent.height), 0.1F, 10.0F);

	ubo.proj[1][1] *= -1;

	memcpy(mUniformBuffersMapped.at(currentImage), &ubo, sizeof(ubo));
}

void VulkanApplication::createImage(ui width, ui height, ui mipLevels, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage,
									vk::MemoryPropertyFlags properties, vk::raii::Image &image, vk::raii::DeviceMemory &imageMemory) const
{
	const vk::ImageCreateInfo imageInfo{.imageType = vk::ImageType::e2D,
										.format = format,
										.extent = {.width = width, .height = height, .depth = 1},
										.mipLevels = mipLevels,
										.arrayLayers = 1,
										.samples = vk::SampleCountFlagBits::e1,
										.tiling = tiling,
										.usage = usage,
										.sharingMode = vk::SharingMode::eExclusive,
										.initialLayout = vk::ImageLayout::eUndefined};
	image = vk::raii::Image(mDevice, imageInfo);

	const vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
	const vk::MemoryAllocateInfo allocInfo{.allocationSize = memRequirements.size,
										   .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)};

	imageMemory = vk::raii::DeviceMemory(mDevice, allocInfo);
	image.bindMemory(imageMemory, 0);
}

ATTR_NODISCARD std::unique_ptr<vk::raii::CommandBuffer> VulkanApplication::beginSingleTimeCommands() const
{
	const vk::CommandBufferAllocateInfo allocInfo{
		.commandPool = mCommandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1};

	std::unique_ptr<vk::raii::CommandBuffer> commandBuffer{
		std::make_unique<vk::raii::CommandBuffer>(std::move(mDevice.allocateCommandBuffers(allocInfo).front()))};

	commandBuffer->begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	return commandBuffer;
}

void VulkanApplication::endSingleTimeCommands(vk::raii::CommandBuffer &commandBuffer) const
{
	commandBuffer.end();

	const vk::SubmitInfo submitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandBuffer};
	mPresentQueue.submit(submitInfo, nullptr);
	mPresentQueue.waitIdle();
}

void VulkanApplication::copyBufferToImage(const vk::raii::Buffer &buffer, vk::raii::Image &image, uint32_t width, uint32_t height)
{
	std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = beginSingleTimeCommands();
	const vk::BufferImageCopy region{.bufferOffset = 0,
									 .bufferRowLength = 0,
									 .bufferImageHeight = 0,
									 .imageSubresource
									 = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
									 .imageOffset = {.x = 0, .y = 0, .z = 0},
									 .imageExtent = {.width = width, .height = height, .depth = 1}};

	commandBuffer->copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, {region});
	endSingleTimeCommands(*commandBuffer);
}

void VulkanApplication::transitionImageLayout(const vk::raii::Image &image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
											  ui mipLevels) const
{
	auto commandBuffer = beginSingleTimeCommands();

	vk::ImageMemoryBarrier barrier{.oldLayout = oldLayout,
								   .newLayout = newLayout,
								   .image = image,
								   .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
														.baseMipLevel = 0,
														.levelCount = mipLevels,
														.baseArrayLayer = 0,
														.layerCount = 1}};

	vk::PipelineStageFlags sourceStage;
	vk::PipelineStageFlags destinationStage;

	if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal)
	{
		barrier.srcAccessMask = {};
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

		sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
		destinationStage = vk::PipelineStageFlagBits::eTransfer;
	}
	else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
	{
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		sourceStage = vk::PipelineStageFlagBits::eTransfer;
		destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
	}
	else
	{
		throw std::invalid_argument("unsupported layout transition!");
	}

	commandBuffer->pipelineBarrier(sourceStage, destinationStage, {}, {}, nullptr, barrier);
	endSingleTimeCommands(*commandBuffer);
}

ATTR_NODISCARD vk::raii::ImageView VulkanApplication::createImageView(vk::raii::Image &image, vk::Format format,
																	  vk::ImageAspectFlags aspectFlags, ui mipLevels) const
{
	const vk::ImageViewCreateInfo viewInfo{
		.image = image,
		.viewType = vk::ImageViewType::e2D,
		.format = format,
		.subresourceRange = {.aspectMask = aspectFlags, .baseMipLevel = 0, .levelCount = mipLevels, .baseArrayLayer = 0, .layerCount = 1}};

	return {mDevice, viewInfo};
}

vk::Format VulkanApplication::findSupportedFormat(const std::vector<vk::Format> &candidates, vk::ImageTiling tiling,
												  vk::FormatFeatureFlags features)
{
	auto formatIt = std::ranges::find_if(candidates, [&](const vk::Format format) noexcept {
		const vk::FormatProperties props{mPhysicalDevice.getFormatProperties(format)};

		return (((tiling == vk::ImageTiling::eLinear) && ((props.linearTilingFeatures & features) == features))
				|| ((tiling == vk::ImageTiling::eOptimal) && ((props.optimalTilingFeatures & features) == features)));
	});

	if (formatIt == candidates.end())
	{
		throw std::runtime_error("Failed to find supported format!");
	}

	return *formatIt;
}

vk::Format VulkanApplication::findDepthFormat()
{
	return findSupportedFormat({vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
							   vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

void VulkanApplication::generateMipmaps(vk::raii::Image &image, vk::Format imageFormat, ui width, ui height, ui mipLevels)
{
	// Check if image format supports linear blit-ing
	const vk::FormatProperties formatProperties = mPhysicalDevice.getFormatProperties(imageFormat);

	if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
	{
		throw std::runtime_error("Texture image format does not support linear blitting!");
	}

	std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = beginSingleTimeCommands();

	vk::ImageMemoryBarrier barrier = {.srcAccessMask = vk::AccessFlagBits::eTransferWrite,
									  .dstAccessMask = vk::AccessFlagBits::eTransferRead,
									  .oldLayout = vk::ImageLayout::eTransferDstOptimal,
									  .newLayout = vk::ImageLayout::eTransferSrcOptimal,
									  .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
									  .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
									  .image = image};
	barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	si mipWidth{sc<si>(width)};
	si mipHeight{sc<si>(height)};

	for (ui i{1}; i < mipLevels; ++i)
	{
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

		commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier);

		vk::ArrayWrapper1D<vk::Offset3D, 2> offsets;
		vk::ArrayWrapper1D<vk::Offset3D, 2> dstOffsets;
		offsets.at(0) = vk::Offset3D(0, 0, 0);
		offsets.at(1) = vk::Offset3D(mipWidth, mipHeight, 1);
		dstOffsets.at(0) = vk::Offset3D(0, 0, 0);
		dstOffsets.at(1) = vk::Offset3D(mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1);
		vk::ImageBlit blit = {.srcSubresource = {}, .srcOffsets = offsets, .dstSubresource = {}, .dstOffsets = dstOffsets};
		blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i - 1, 0, 1);
		blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);

		commandBuffer->blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal, {blit},
								 vk::Filter::eLinear);

		barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {},
									   barrier);

		if (mipWidth > 1)
		{
			mipWidth /= 2;
		}
		if (mipHeight > 1)
		{
			mipHeight /= 2;
		}
	}

	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

	commandBuffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);

	endSingleTimeCommands(*commandBuffer);
}

void VulkanApplication::mainLoop()
{
	while (glfwWindowShouldClose(mWindow.get()) == 0)
	{
		glfwPollEvents();
		drawFrame();
	}

	mDevice.waitIdle();
}

void VulkanApplication::drawFrame()
{
	// Note: inFlightFences, presentCompleteSemaphores, and commandBuffers are indexed by frameIndex, while renderFinishedSemaphores is
	// indexed by imageIndex
	const vk::Result fenceResult = mDevice.waitForFences(*mInFlightFences.at(mFrameIndex), vk::True, std::numeric_limits<ul>::max());

	if (fenceResult != vk::Result::eSuccess)
	{
		throw std::runtime_error("Failed to wait for in-flight fence!");
	}

	auto [result, imageIndex]
		= mSwapChain.acquireNextImage(std::numeric_limits<ul>::max(), *mPresentCompleteSemaphores.at(mFrameIndex), nullptr);

	if (result == vk::Result::eErrorOutOfDateKHR)
	{
		recreateSwapChain();
		return;
	}

	if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
	{
		assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
		throw std::runtime_error("Failed to acquire swap chain image!");
	}

	updateUniformBuffer(mFrameIndex);

	mDevice.resetFences(*mInFlightFences.at(mFrameIndex));

	mCommandBuffers.at(mFrameIndex).reset();

	recordCommandBuffer(imageIndex);

	const vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
	const vk::SubmitInfo submitInfo{.waitSemaphoreCount = 1,
									.pWaitSemaphores = &*mPresentCompleteSemaphores.at(mFrameIndex),
									.pWaitDstStageMask = &waitDestinationStageMask,
									.commandBufferCount = 1,
									.pCommandBuffers = &*mCommandBuffers.at(mFrameIndex),
									.signalSemaphoreCount = 1,
									.pSignalSemaphores = &*mRenderCompleteSemaphores.at(imageIndex)};

	mPresentQueue.submit(submitInfo, *mInFlightFences.at(mFrameIndex));

	const vk::PresentInfoKHR presentInfo{.waitSemaphoreCount = 1,
										 .pWaitSemaphores = &*mRenderCompleteSemaphores.at(imageIndex),
										 .swapchainCount = 1,
										 .pSwapchains = &*mSwapChain,
										 .pImageIndices = &imageIndex};
	result = mPresentQueue.presentKHR(presentInfo);

	// Due to VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS being defined, eErrorOutOfDateKHR can be checked as a result
	// here and does not need to be caught by an exception.
	if ((result == vk::Result::eSuboptimalKHR) || (result == vk::Result::eErrorOutOfDateKHR) || mFramebufferResized)
	{
		mFramebufferResized = false;
		recreateSwapChain();
	}
	else
	{
		// There are no other success codes than eSuccess; on any error code, presentKHR already threw an exception.
		assert(result == vk::Result::eSuccess);
	}

	mFrameIndex = (mFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanApplication::cleanup()
{
	glfwDestroyWindow(mWindow.get());

	glfwTerminate();
}