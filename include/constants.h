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
#include <string>

#include "attributeMacros.h"
#include "typedefs.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>

ATTR_MAYBE_UNUSED constexpr ui WIDTH{800};
ATTR_MAYBE_UNUSED constexpr ui HEIGHT{600};

ATTR_MAYBE_UNUSED constexpr std::array<const char *, 1> validationLayers{"VK_LAYER_KHRONOS_validation"};
ATTR_MAYBE_UNUSED constexpr std::array<const char *, 2> requiredDeviceExtensions
	= {vk::KHRSwapchainExtensionName, vk::KHRShaderDrawParametersExtensionName};

ATTR_MAYBE_UNUSED constexpr ui MAX_FRAMES_IN_FLIGHT{2};

const std::string MODEL_PATH{"resources/models/viking_room.obj"};
const std::string TEXTURE_PATH{"resources/textures/viking_room.png"};

#ifdef NDEBUG
constexpr bool enableValidationLayers{false};
#else
constexpr bool enableValidationLayers{true};
#endif

#endif