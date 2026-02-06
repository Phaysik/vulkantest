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
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "attributeMacros.h"
#include "constants.h"
#include "typedefs.h"

#ifndef VULKAN_HPP_NO_CONSTRUCTORS
	#define VULKAN_HPP_NO_CONSTRUCTORS true
#endif

#ifndef VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
	#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS true
#endif
#include <vulkan/vulkan_raii.hpp>

#ifndef GLFW_INCLUDE_VULKAN
	#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

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
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	mWindow = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>(glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr),
																		glfwDestroyWindow);
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
	createGraphicsPipeline();
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
			features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering
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
	si queueIndex{-1};
	for (ui qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
	{
		if ((queueFamilyProperties.at(qfpIndex).queueFlags & vk::QueueFlagBits::eGraphics)
			&& mPhysicalDevice.getSurfaceSupportKHR(qfpIndex, *mSurface) == VK_TRUE)
		{
			// found a queue family that supports both graphics and present
			queueIndex = sc<si>(qfpIndex);
			break;
		}
	}
	if (queueIndex == -1)
	{
		throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
	}

	// Query for Vulkan 1.3 features
	const vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features,
							 vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
		featureChain{
			{},								  // vk::PhysicalDeviceFeatures2 (empty for now)
			{.dynamicRendering = VK_TRUE},	  // Enable dynamic rendering from Vulkan 1.3
			{.extendedDynamicState = VK_TRUE} // Enable extended dynamic state from the extension
		};

	// Create a device
	constexpr float queuePriority{0.5F};
	const vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
		.queueFamilyIndex = sc<ui>(queueIndex), .queueCount = 1, .pQueuePriorities = &queuePriority};

	const vk::DeviceCreateInfo deviceCreateInfo{.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
												.queueCreateInfoCount = 1,
												.pQueueCreateInfos = &deviceQueueCreateInfo,
												.enabledExtensionCount = sc<ui>(requiredDeviceExtensions.size()),
												.ppEnabledExtensionNames = requiredDeviceExtensions.data()};

	mDevice = vk::raii::Device(mPhysicalDevice, deviceCreateInfo);
	mPresentQueue = vk::raii::Queue(mDevice, sc<ui>(queueIndex), 0);
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

void VulkanApplication::createGraphicsPipeline()
{
	const vk::raii::ShaderModule shaderModule{createShaderModule(readFile("resources/shaders/triangle.spv"))};

	const vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
		.stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule, .pName = "vertMain"};
	const vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
		.stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "fragMain"};

	ATTR_MAYBE_UNUSED const std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{vertShaderStageInfo, fragShaderStageInfo};

	const vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
	const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = vk::PrimitiveTopology::eTriangleList};
	const vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1, .scissorCount = 1};

	const vk::PipelineRasterizationStateCreateInfo rasterizer{.depthClampEnable = vk::False,
															  .rasterizerDiscardEnable = vk::False,
															  .polygonMode = vk::PolygonMode::eFill,
															  .cullMode = vk::CullModeFlagBits::eBack,
															  .frontFace = vk::FrontFace::eClockwise,
															  .depthBiasEnable = vk::False,
															  .depthBiasSlopeFactor = 1.0F,
															  .lineWidth = 1.0F};

	const vk::PipelineMultisampleStateCreateInfo multisampling{.rasterizationSamples = vk::SampleCountFlagBits::e1,
															   .sampleShadingEnable = vk::False};

	const vk::PipelineColorBlendAttachmentState colorBlendAttachment{.blendEnable = vk::False,
																	 .colorWriteMask
																	 = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
																	 | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

	const vk::PipelineColorBlendStateCreateInfo colorBlending{
		.logicOpEnable = vk::False, .logicOp = vk::LogicOp::eCopy, .attachmentCount = 1, .pAttachments = &colorBlendAttachment};

	const std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
	const vk::PipelineDynamicStateCreateInfo dynamicState{.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
														  .pDynamicStates = dynamicStates.data()};

	const vk::PipelineLayoutCreateInfo pipelineLayoutInfo{.setLayoutCount = 0, .pushConstantRangeCount = 0};

	mPipelineLayout = vk::raii::PipelineLayout(mDevice, pipelineLayoutInfo);

	vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain
		= {{.stageCount = 2,
			.pStages = shaderStages.data(),
			.pVertexInputState = &vertexInputInfo,
			.pInputAssemblyState = &inputAssembly,
			.pViewportState = &viewportState,
			.pRasterizationState = &rasterizer,
			.pMultisampleState = &multisampling,
			.pColorBlendState = &colorBlending,
			.pDynamicState = &dynamicState,
			.layout = mPipelineLayout,
			.renderPass = nullptr},
		   {
			   .colorAttachmentCount = 1,
			   .pColorAttachmentFormats = &mSwapChainSurfaceFormat.format,
		   }};

	mGraphicsPipeline = vk::raii::Pipeline(mDevice, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

ATTR_NODISCARD vk::raii::ShaderModule VulkanApplication::createShaderModule(const std::vector<char> &code) const
{
	const vk::ShaderModuleCreateInfo createInfo{.codeSize = code.size() * sizeof(char), .pCode = reinterpret_cast<const ui *>(code.data())};

	vk::raii::ShaderModule shaderModule{mDevice, createInfo};

	return shaderModule;
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

void VulkanApplication::mainLoop()
{
	while (glfwWindowShouldClose(mWindow.get()) == 0)
	{
		glfwPollEvents();
	}
}

void VulkanApplication::cleanup()
{
	glfwDestroyWindow(mWindow.get());

	glfwTerminate();
}