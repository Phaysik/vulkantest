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

#include <iostream>
#include <memory>
#include <vector>

#include "attributeMacros.h"

#include <GLFW/glfw3.h>

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
									  const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger);

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator);

class HelloTriangleApplication
{
	public:
		HelloTriangleApplication() noexcept : window(nullptr, glfwDestroyWindow) {}

		HelloTriangleApplication(const HelloTriangleApplication &) = delete;
		HelloTriangleApplication &operator=(const HelloTriangleApplication &) = delete;
		~HelloTriangleApplication() = default;
		HelloTriangleApplication(HelloTriangleApplication &&) = default;
		HelloTriangleApplication &operator=(HelloTriangleApplication &&) = default;

		void run();

	private:
		void initWindow();

		void initVulkan();

		void setupDebugMessenger();

		void mainLoop();

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

	private:
		std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window;
		VkInstance instance{};
		VkDebugUtilsMessengerEXT debugMessenger{};
};

#endif