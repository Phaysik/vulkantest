/*! @file entityRecord.h
	@brief Defines `EntityRecord` which maps an entity to its storage location.
	@details `EntityRecord` is a lightweight POD used by the entity registry to store the generation and location (archetype id, chunk
   index, slot index) of an entity. The record is stored in dense arrays and should remain trivially copyable.
	@date 02/14/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_ENTITYRECORD_H
#define INCLUDE_ECS_ENTITYRECORD_H

#include "Core/typedefs.h"
#include "ECS/entity.h"

namespace Dimensia::ECS
{
	using Dimensia::Core::ui;

	/*! @var INVALID_ARCHETYPE_ID
		@brief Special value indicating an invalid or missing archetype identifier.
		@details Set to `UINT32_MAX`. Consumers should compare against this constant
		when an archetype lookup fails or an entity is not assigned to any archetype.
	*/
	inline constexpr ui INVALID_ARCHETYPE_ID{UINT32_MAX};

	/*! @enum State
		@brief Lifecycle state of an entity, stored in `EntityRecord`.
	*/
	enum class State : Dimensia::Core::ub
	{
		Uninitialized,
		Active,
		Destroying,
		Destroyed
	};

	/*! @struct EntityRecord
		@brief Lightweight record storing an entity's generation, storage location, and lifecycle state.
		@details Fields are POD and default-initialized to zero. The record contains:
		- `generation`: incarnation counter used to detect stale handles
		- `archetypeID`: index of the archetype containing the entity or @ref INVALID_ARCHETYPE_ID
		- `chunkIndex`: index of the chunk within the archetype
		- `slotIndex`: slot index within the chunk
		- `state`: lifecycle state of the entity
		@note Thread-safety and synchronization are the responsibility of the caller.
	*/
	struct EntityRecord
	{
			/*! @var generation
				@brief Entity generation (incarnation) counter used to validate handles.
				@details Incremented each time an entity id is recycled.
			*/
			ui generation{};

			/*! @var archetypeID
				@brief Identifier of the archetype that currently stores the entity.
				@details Use @ref INVALID_ARCHETYPE_ID to indicate the entity is not present in any archetype.
			*/
			ui archetypeID{};

			/*! @var chunkIndex
				@brief Index of the chunk inside the archetype where the entity resides.
			*/
			ui chunkIndex{};

			/*! @var slotIndex
				@brief Slot index within the chunk corresponding to the entity.
			*/
			ui slotIndex{};

			/*! @var state
				@brief Lifecycle state of the entity.
				@details Tracks whether the entity is active, being destroyed, etc.
			*/
			State state{State::Uninitialized};
	};
} // namespace Dimensia::ECS

#endif