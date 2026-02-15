/*! \file entity.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_ENTITY_H
#define INCLUDE_ECS_ENTITY_H

#include <cstdint>

namespace Dimensia::ECS
{
	struct Entity
	{
			uint32_t index;
			uint32_t generation;
			bool operator==(const Entity &other) const = default;
			bool operator!=(const Entity &other) const = default;
	};

	constexpr Entity NULL_ENTITY{.index = 0, .generation = 0};
} // namespace Dimensia::ECS

#endif