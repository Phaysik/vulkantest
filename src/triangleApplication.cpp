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
	createGraphicsPipeline();
	createCommandPool();
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
			features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2
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
			{},															// vk::PhysicalDeviceFeatures2 (empty for now)
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

void VulkanApplication::createCommandPool()
{
	const vk::CommandPoolCreateInfo poolInfo{.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
											 .queueFamilyIndex = sc<ui>(mQueueIndex)};

	mCommandPool = vk::raii::CommandPool(mDevice, poolInfo);
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

	// Before starting rendering, transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
	transition_image_layout(imageIndex, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
							{},													// srcAccessMask (no need to wait for previous operations)
							vk::AccessFlagBits2::eColorAttachmentWrite,			// dstAccessMask
							vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
							vk::PipelineStageFlagBits2::eColorAttachmentOutput	// dstStage
	);

	const vk::ClearValue clearColor{vk::ClearColorValue(std::array<float, 4>{0.0F, 0.0F, 0.0F, 1.0F})};
	const vk::RenderingAttachmentInfo attachmentInfo{
		.imageView = mSwapChainImageViews.at(imageIndex),
		.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
		.loadOp = vk::AttachmentLoadOp::eClear,
		.storeOp = vk::AttachmentStoreOp::eStore,
		.clearValue = clearColor,
	};

	const vk::RenderingInfo renderingInfo = {.renderArea = {.offset = {.x = 0, .y = 0}, .extent = mSwapChainExtent},
											 .layerCount = 1,
											 .colorAttachmentCount = 1,
											 .pColorAttachments = &attachmentInfo};

	commandBuffer.beginRendering(renderingInfo);
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, mGraphicsPipeline);
	commandBuffer.setViewport(0, vk::Viewport{.x = 0.0F,
											  .y = 0.0F,
											  .width = sc<float>(mSwapChainExtent.width),
											  .height = sc<float>(mSwapChainExtent.height),
											  .minDepth = 0.0F,
											  .maxDepth = 1.0F});
	commandBuffer.setScissor(0, vk::Rect2D{.offset = {.x = 0, .y = 0}, .extent = mSwapChainExtent});
	commandBuffer.draw(3, 1, 0, 0);
	commandBuffer.endRendering();

	// After rendering, transition the swapchain image to PRESENT_SRC
	transition_image_layout(imageIndex, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
							vk::AccessFlagBits2::eColorAttachmentWrite,			// srcAccessMask
							{},													// dstAccessMask
							vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
							vk::PipelineStageFlagBits2::eBottomOfPipe			// dstStage
	);

	commandBuffer.end();
}

void VulkanApplication::transition_image_layout(ui imageIndex, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
												vk::AccessFlags2 srcAccessMask, vk::AccessFlags2 dstAccessMask,
												vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask)
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
		.image = mSwapChainImages.at(imageIndex),
		.subresourceRange
		= {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
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

	// Recreate command buffers and sync objects so they match the new swapchain
	createCommandBuffers();
	createSyncObjects();
}

void VulkanApplication::cleanupSwapChain()
{
	// Ensure the device is idle before destroying swapchain-dependent resources
	mDevice.waitIdle();

	mCommandBuffers.clear();
	mSwapChainImageViews.clear();
	mSwapChain = nullptr;

	// Destroy and clear synchronization primitives so they can be recreated
	mRenderCompleteSemaphores.clear();
	mPresentCompleteSemaphores.clear();
	mInFlightFences.clear();
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
	cleanupSwapChain();

	glfwDestroyWindow(mWindow.get());

	glfwTerminate();
}