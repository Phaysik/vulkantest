/*! \file entity.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_ENTITY_H
#define INCLUDE_ECS_ENTITY_H

#include <compare>

#include "Core/typedefs.h"

namespace Dimensia::ECS
{
	using Dimensia::Core::ui;

	struct Entity
	{
		public:
			// MARK: Comparison
			std::strong_ordering operator<=>(const Entity &other) const noexcept = default;

			// MARK: Member Variables

			// NOLINTBEGIN(misc-non-private-member-variables-in-classes)

			ui index{};
			ui generation{};

			// NOLINTEND(misc-non-private-member-variables-in-classes)
	};

	constexpr Entity NULL_ENTITY{.index = 0, .generation = 0};
} // namespace Dimensia::ECS

#endif