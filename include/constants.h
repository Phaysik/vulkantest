/*! \file constants.h
	\brief Contains the constants for the triangle application
	\date 02/04/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#ifndef INCLUDE_CONSTANTS_H
#define INCLUDE_CONSTANTS_H

#include <array>

#include "typedefs.h"

#ifndef VULKAN_HPP_NO_CONSTRUCTORS
	#define VULKAN_HPP_NO_CONSTRUCTORS
#endif

#ifndef VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
	#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#endif
#ifndef VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
	#define VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
#endif
#include <vulkan/vulkan_raii.hpp>

#ifndef GLFW_INCLUDE_VULKAN
	#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

constexpr ui WIDTH{800};
constexpr ui HEIGHT{600};

constexpr std::array<const char *, 1> validationLayers{"VK_LAYER_KHRONOS_validation"};
constexpr std::array<const char *, 2> requiredDeviceExtensions = {vk::KHRSwapchainExtensionName, vk::KHRShaderDrawParametersExtensionName};

constexpr ui MAX_FRAMES_IN_FLIGHT{2};

#ifdef NDEBUG
constexpr bool enableValidationLayers{false};
#else
constexpr bool enableValidationLayers{true};
#endif

#endif