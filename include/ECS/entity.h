/*! @file entity.h
	@brief Defines the lightweight `Entity` identifier used by the ECS registry.
	@details `Entity` is a trivially-copyable identifier containing an `index` and a `generation` counter. The `generation` field is used to
   validate recycled entity handles. `NULL_ENTITY` is provided as the zero/invalid sentinel.
	@date 02/14/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_ENTITY_H
#define INCLUDE_ECS_ENTITY_H

#include <compare>

#include "Core/typedefs.h"

namespace Dimensia::ECS
{
	using Dimensia::Core::ui;

	/*! @struct Entity
		@brief Lightweight identifier for an entity in the ECS.
		@details The `Entity` struct contains an `index` into the registry's entity arrays and a `generation` counter used to detect stale
	   handles. The defaulted three-way comparison provides ordering suitable for sorting or associative containers.
		@note Entity is intentionally kept small (8 bytes) for cache-friendly storage in archetype chunks. Lifecycle state is stored
	   separately in `EntityRecord`.
	*/
	struct Entity
	{
		public:
			// MARK: Comparison Operator

			/*! @brief Defaulted three-way comparison for `Entity`.
				@return A `std::strong_ordering` reflecting lexicographic comparison of `index` then `generation`.
			*/
			std::strong_ordering operator<=>(const Entity &other) const noexcept = default;

			// MARK: Member Variables

			// NOLINTBEGIN(misc-non-private-member-variables-in-classes)

			/*! @var index
				@brief Dense index of the entity within registry storage.
			*/
			ui index{};

			/*! @var generation
				@brief Generation (incarnation) counter used to validate entity handles.
				@details Incremented when an `index` is recycled to prevent use-after-free style errors.
			*/
			ui generation{};

			// NOLINTEND(misc-non-private-member-variables-in-classes)
	};

	/*! @var NULL_ENTITY
		@brief Sentinel value representing a null/invalid entity.
		@details Uses `UINT32_MAX` for `index` and zero for `generation` so it cannot collide with any valid entity handle
	   (valid entities start at index 0 with generation >= 1). Use this value to represent an empty handle where appropriate.
	*/
	constexpr Entity NULL_ENTITY{.index = UINT32_MAX, .generation = 0};
} // namespace Dimensia::ECS

#endif