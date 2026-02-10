/*! \file vertex.h
	\brief Contains the function declarations for the Vertex struct used in the triangle application
	\date 02/06/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/
#include <array>

#include "attributeMacros.h"
#include "typedefs.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

struct Vertex
{
		glm::vec3 pos;
		glm::vec3 color;
		glm::vec2 texCoord;

		static vk::VertexInputBindingDescription getBindingDescription()
		{
			return vk::VertexInputBindingDescription{.binding = 0, .stride = sizeof(Vertex), .inputRate = vk::VertexInputRate::eVertex};
		}

		static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions()
		{
			return {vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
					vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)),
					vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord))};
		}
};

ATTR_MAYBE_UNUSED constexpr std::array<Vertex, 8> vertices
	= {{{.pos = {-0.5F, -0.5F, 0.0F}, .color = {1.0F, 0.0F, 0.0F}, .texCoord = {0.0F, 0.0F}},
		{.pos = {0.5F, -0.5F, 0.0F}, .color = {0.0F, 1.0F, 0.0F}, .texCoord = {1.0F, 0.0F}},
		{.pos = {0.5F, 0.5F, 0.0F}, .color = {0.0F, 0.0F, 1.0F}, .texCoord = {1.0F, 1.0F}},
		{.pos = {-0.5F, 0.5F, 0.0F}, .color = {1.0F, 1.0F, 1.0F}, .texCoord = {0.0F, 1.0F}},

		{.pos = {-0.5F, -0.5F, -0.5F}, .color = {1.0F, 0.0F, 0.0F}, .texCoord = {0.0F, 0.0F}},
		{.pos = {0.5F, -0.5F, -0.5F}, .color = {0.0F, 1.0F, 0.0F}, .texCoord = {1.0F, 0.0F}},
		{.pos = {0.5F, 0.5F, -0.5F}, .color = {0.0F, 0.0F, 1.0F}, .texCoord = {1.0F, 1.0F}},
		{.pos = {-0.5F, 0.5F, -0.5F}, .color = {1.0F, 1.0F, 1.0F}, .texCoord = {0.0F, 1.0F}}}};

ATTR_MAYBE_UNUSED constexpr std::array<us, 12> indices = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};