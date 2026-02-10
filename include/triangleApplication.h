/*! \file triangleApplication.h
	\brief Contains the function declarations for creating a Vulkan Renderer that displays a triangle
	\date 02/06/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#ifndef INCLUDE_TRIANGLEAPPLICATION_H
#define INCLUDE_TRIANGLEAPPLICATION_H

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include "attributeMacros.h"
#include "constants.h"
#include "typedefs.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

struct Vertex
{
		glm::vec2 pos;
		glm::vec3 color;

		static vk::VertexInputBindingDescription getBindingDescription()
		{
			return vk::VertexInputBindingDescription{.binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex};
		}

		static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
		{
			return {vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, pos)),
					vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color))};
		}
};

constexpr std::array<Vertex, 4> vertices = {{{.pos = {-0.5F, -0.5F}, .color = {1.0F, 0.0F, 0.0F}},
											 {.pos = {0.5F, -0.5F}, .color = {0.0F, 1.0F, 0.0F}},
											 {.pos = {0.5F, 0.5F}, .color = {0.0F, 0.0F, 1.0F}},
											 {.pos = {-0.5F, 0.5F}, .color = {1.0F, 1.0F, 1.0F}}}};

constexpr std::array<us, 6> indices = {0, 1, 2, 2, 3, 0};

struct UniformBufferObject
{
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 proj;
};

class VulkanApplication
{
	public:
		VulkanApplication() : mSwapChainImages({}) {}

	public:
		void run();

	private:
		void initWindow();

		void initVulkan();

		void createInstance();

		void setupDebugMessenger();

		void createSurface();

		void pickPhysicalDevice();

		void createLogicalDevice();

		void createSwapChain();

		void createImageViews();

		void createDescriptorSetLayout();

		void createGraphicsPipeline();

		void createCommandPool();

		void createVertexBuffer();

		void createIndexBuffer();

		void createUniformBuffers();

		void createDescriptorPool();

		void createDescriptorSets();

		void createCommandBuffers();

		void createSyncObjects();

		static std::vector<const char *> getRequiredExtensions()
		{
			ui glfwExtensionCount{0};
			auto *glfwExtensions{glfwGetRequiredInstanceExtensions(&glfwExtensionCount)};

			std::vector<const char *> extensions;
			extensions.reserve(glfwExtensionCount);

			if (glfwExtensions != nullptr && glfwExtensionCount > 0)
			{
				std::copy_n(glfwExtensions, glfwExtensionCount, std::back_inserter(extensions));
			}

			if (enableValidationLayers)
			{
				extensions.push_back(vk::EXTDebugUtilsExtensionName);
			}

			return extensions;
		}

		static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats)
		{
			assert(!availableFormats.empty());

			const auto formatIt{std::ranges::find_if(availableFormats, [](const vk::SurfaceFormatKHR &availableFormat) noexcept {
				return availableFormat.format == vk::Format::eB8G8R8A8Srgb
					&& availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
			})};

			return formatIt != availableFormats.end() ? *formatIt : availableFormats.front();
		}

		static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes)
		{
			assert(std::ranges::any_of(availablePresentModes, [](const vk::PresentModeKHR presentMode) noexcept {
				return presentMode == vk::PresentModeKHR::eFifo;
			}));

			return std::ranges::any_of(availablePresentModes,
									   [](const vk::PresentModeKHR value) noexcept { return vk::PresentModeKHR::eMailbox == value; })
					 ? vk::PresentModeKHR::eMailbox
					 : vk::PresentModeKHR::eFifo;
		}

		static ui chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR &surfaceCapabilities)
		{
			ui minImageCount{std::max(3U, surfaceCapabilities.minImageCount)};

			if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
			{
				minImageCount = surfaceCapabilities.maxImageCount;
			}
			return minImageCount;
		}

		vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities);

		ATTR_NODISCARD vk::raii::ShaderModule createShaderModule(const std::vector<char> &code) const;

		void recordCommandBuffer(ui imageIndex);

		void transition_image_layout(ui imageIndex, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 srcAccessMask,
									 vk::AccessFlags2 dstAccessMask, vk::PipelineStageFlags2 srcStageMask,
									 vk::PipelineStageFlags2 dstStageMask);

		void recreateSwapChain();

		void cleanupSwapChain();

		ui findMemoryType(const ui typeFilter, vk::MemoryPropertyFlags properties);

		void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer &buffer,
						  vk::raii::DeviceMemory &bufferMemory);

		void copyBuffer(vk::raii::Buffer &srcBuffer, vk::raii::Buffer &dstBuffer, vk::DeviceSize size);

		void updateUniformBuffer(ui currentImage);

		void mainLoop();

		void drawFrame();

		void cleanup();

		static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
															  vk::DebugUtilsMessageTypeFlagsEXT type,
															  const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
															  ATTR_MAYBE_UNUSED void *pUserData)
		{
			if (severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
			{
				std::string messageType{(type & vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral) ? "[GENERAL]" : ""};
				messageType += (type & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation) ? "[VALIDATION]" : "";
				messageType += (type & vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance) ? "[PERFORMANCE]" : "";

				std::cerr << messageType << ": validation layer: " << to_string(type) << " msg: " << pCallbackData->pMessage << '\n';
			}

			return vk::False;
		}

		static std::vector<char> readFile(const std::filesystem::path &filePath)
		{
			std::ifstream file{filePath, std::ios::ate | std::ios::binary};

			if (!file.is_open())
			{
				throw std::runtime_error("Failed to open file: " + filePath.string());
			}

			std::vector<char> buffer(sc<ui>(file.tellg()));

			file.seekg(0, std::ios::beg);
			file.read(buffer.data(), sc<std::streamsize>(buffer.size()));

			file.close();

			return buffer;
		}

		static void framebufferResizeCallback(GLFWwindow *window, ATTR_MAYBE_UNUSED si width, ATTR_MAYBE_UNUSED si height)
		{
			auto *app = reinterpret_cast<VulkanApplication *>(glfwGetWindowUserPointer(window));
			app->mFramebufferResized = true;
		}

	private:
		std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> mWindow{nullptr, glfwDestroyWindow};

		vk::raii::Context mContext{};
		vk::raii::Instance mInstance{nullptr};

		vk::raii::DebugUtilsMessengerEXT mDebugMessenger{nullptr};

		vk::raii::SurfaceKHR mSurface{nullptr};

		vk::raii::PhysicalDevice mPhysicalDevice{nullptr};

		si mQueueIndex{-1};
		vk::raii::Device mDevice{nullptr};

		vk::raii::Queue mPresentQueue{nullptr};

		vk::raii::SwapchainKHR mSwapChain{nullptr};
		std::vector<vk::Image> mSwapChainImages;
		vk::SurfaceFormatKHR mSwapChainSurfaceFormat{.format = vk::Format::eUndefined};
		vk::Extent2D mSwapChainExtent{};
		std::vector<vk::raii::ImageView> mSwapChainImageViews{};

		vk::raii::DescriptorSetLayout mDescriptorSetLayout{nullptr};
		vk::raii::DescriptorPool mDescriptorPool{nullptr};
		std::vector<vk::raii::DescriptorSet> mDescriptorSets{};

		vk::raii::PipelineLayout mPipelineLayout{nullptr};
		vk::raii::Pipeline mGraphicsPipeline{nullptr};

		vk::raii::CommandPool mCommandPool{nullptr};
		std::vector<vk::raii::CommandBuffer> mCommandBuffers{};

		std::vector<vk::raii::Semaphore> mPresentCompleteSemaphores{};
		std::vector<vk::raii::Semaphore> mRenderCompleteSemaphores{};
		std::vector<vk::raii::Fence> mInFlightFences{};

		ui mFrameIndex{0};
		bool mFramebufferResized{false};

		vk::raii::Buffer mVertexBuffer{nullptr};
		vk::raii::DeviceMemory mVertexBufferMemory{nullptr};

		vk::raii::Buffer mIndexBuffer{nullptr};
		vk::raii::DeviceMemory mIndexBufferMemory{nullptr};

		std::vector<vk::raii::Buffer> mUniformBuffers{};
		std::vector<vk::raii::DeviceMemory> mUniformBuffersMemory{};
		std::vector<void *> mUniformBuffersMapped{};
};

#endif