/*! \file triangleApplication.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/04/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#ifndef INCLUDE_TRIANGLEAPPLICATION_H
#define INCLUDE_TRIANGLEAPPLICATION_H

#ifndef GLFW_INCLUDE_VULKAN
	#define GLFW_INCLUDE_VULKAN
#endif

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include "attributeMacros.h"
#include "typedefs.h"

#include <GLFW/glfw3.h>

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
									  const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger);

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator);

class HelloTriangleApplication
{
	public:
		HelloTriangleApplication() noexcept
			: mWindow(nullptr, glfwDestroyWindow), mSwapChainImages({}), mSwapChainImageViews({}), mSwapChainFramebuffers({})
		{}

		HelloTriangleApplication(const HelloTriangleApplication &) = delete;
		HelloTriangleApplication &operator=(const HelloTriangleApplication &) = delete;

		~HelloTriangleApplication() {}

		HelloTriangleApplication(HelloTriangleApplication &&) = default;
		HelloTriangleApplication &operator=(HelloTriangleApplication &&) = default;

		void run();

	private:
		struct QueueFamilyIndices
		{
				QueueFamilyIndices() : graphicsFamily({}), presentFamily({}) {}

				std::optional<ui> graphicsFamily;
				std::optional<ui> presentFamily;

				ATTR_NODISCARD bool isComplete() const
				{
					return graphicsFamily.has_value() && presentFamily.has_value();
				}
		};

		struct SwapChainSupportDetails
		{
				SwapChainSupportDetails() : capabilities({}), formats({}), presentModes({}) {}

				~SwapChainSupportDetails() {}

				VkSurfaceCapabilitiesKHR capabilities;
				std::vector<VkSurfaceFormatKHR> formats;
				std::vector<VkPresentModeKHR> presentModes;
		};

		void initWindow();

		void initVulkan();

		void setupDebugMessenger();

		void createSurface();

		bool isDeviceSuitable(VkPhysicalDevice device);

		bool checkDeviceExtensionSupport(VkPhysicalDevice device);

		void pickPhysicalDevice();

		void createLogicalDevice();

		void createSwapChain();

		void createImageViews();

		void createRenderPass();

		void createGraphicsPipeline();

		void createFramebuffers();

		void createCommandPool();

		void createCommandBuffer();

		void createSyncObjects();

		QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

		SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

		VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);

		VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes);

		VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);

		VkShaderModule createShaderModule(const std::vector<char> &code);

		void recordCommandBuffer(VkCommandBuffer commandBuffer, ui imageIndex);

		void mainLoop();

		void drawFrame();

		void cleanup();

		void createInstance();

		bool checkValidationLayerSupport();

		std::vector<const char *> getRequiredExtensions();

		void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo);

		static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
															VkDebugUtilsMessageTypeFlagsEXT messageType,
															const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
															ATTR_MAYBE_UNUSED void *pUserData)
		{
			if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			{
				std::string typeMessage{(messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) != 0 ? "GENERAL" : ""};
				typeMessage += (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0 ? "VALIDATION" : "";
				typeMessage += (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) != 0 ? "PERFORMANCE" : "";

				std::cerr << typeMessage << ": validation layer " << pCallbackData->pMessage << '\n';
			}

			return VK_FALSE;
		}

		static std::vector<char> readFile(const std::filesystem::path &path)
		{
			std::ifstream file{path, std::ios::ate | std::ios::binary};

			if (!file.is_open())
			{
				throw std::runtime_error("Failed to open file!");
			}

			const si fileSize{sc<si>(file.tellg())};
			std::vector<char> buffer(sc<std::size_t>(fileSize));

			file.seekg(0);
			file.read(buffer.data(), fileSize);

			file.close();

			return buffer;
		}

		void recreateSwapChain();

		void cleanupSwapChain();

		static void framebufferResizeCallback(GLFWwindow *window, ATTR_MAYBE_UNUSED si width, ATTR_MAYBE_UNUSED si height)
		{
			auto *app = reinterpret_cast<HelloTriangleApplication *>(glfwGetWindowUserPointer(window));
			app->mFramebufferResized = true;
		}

	private:
		std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> mWindow;

		VkInstance mInstance{};
		VkDebugUtilsMessengerEXT mDebugMessenger{};
		VkSurfaceKHR mSurface{};

		VkPhysicalDevice mPhysicalDevice{VK_NULL_HANDLE};
		VkDevice mDevice{};

		VkQueue mGraphicsQueue{};
		VkQueue mPresentQueue{};

		VkSwapchainKHR mSwapChain{};
		std::vector<VkImage> mSwapChainImages;

		VkFormat mSwapChainImageFormat{};
		VkExtent2D mSwapChainExtent{};

		std::vector<VkImageView> mSwapChainImageViews;

		VkRenderPass mRenderPass{};
		VkPipelineLayout mPipelineLayout{};

		VkPipeline mGraphicsPipeline{};

		std::vector<VkFramebuffer> mSwapChainFramebuffers;

		VkCommandPool mCommandPool{};
		VkCommandBuffer mCommandBuffer{};

		std::vector<VkSemaphore> mImageAvailableSemaphores{};
		std::vector<VkSemaphore> mRenderFinishedSemaphores{};
		std::vector<VkFence> mInFlightFences{};
		ui mCurrentFrame{0};

		bool mFramebufferResized{false};
};

#endif