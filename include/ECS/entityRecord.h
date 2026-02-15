/*! \file entityRecord.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_ENTITYRECORD_H
#define INCLUDE_ECS_ENTITYRECORD_H

#include <cstdint>

namespace Dimensia::ECS
{
	// No forward declaration needed – we store an ID instead of a pointer.
	static constexpr uint32_t INVALID_ARCHETYPE_ID = UINT32_MAX;

	struct EntityRecord
	{
			uint32_t generation;
			uint32_t archetypeId; // ID of the archetype (index into ECS::archetypePtrs_)
			uint32_t chunkIndex;
			uint32_t slotIndex;
	};
} // namespace Dimensia::ECS

#endif