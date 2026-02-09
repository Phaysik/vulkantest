/*! \file constants.h
	\brief Contains the constants for the triangle application
	\date 02/04/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#ifndef INCLUDE_CONSTANTS_H
#define INCLUDE_CONSTANTS_H

#ifndef GLFW_INCLUDE_VULKAN
	#define GLFW_INCLUDE_VULKAN
#endif
#include <array>

#include "typedefs.h"

#include <GLFW/glfw3.h>

const ui WIDTH{800};
const ui HEIGHT{600};

constexpr std::array<const char *, 1> validationLayers{"VK_LAYER_KHRONOS_validation"};
constexpr std::array<const char *, 1> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

constexpr ui MAX_FRAMES_IN_FLIGHT{2};

#ifdef NDEBUG
const bool enableValidationLayers{false};
#else
const bool enableValidationLayers{true};
#endif

#endif