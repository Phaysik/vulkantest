/*! \file entityRecord.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_ENTITYRECORD_H
#define INCLUDE_ECS_ENTITYRECORD_H

#include "Core/typedefs.h"

namespace Dimensia::ECS
{
	using Dimensia::Core::ui;

	// No forward declaration needed – we store an ID instead of a pointer.
	static constexpr ui INVALID_ARCHETYPE_ID{UINT32_MAX};

	struct EntityRecord
	{
			ui generation{};
			ui archetypeId{}; // ID of the archetype (index into ECS::archetypePtrs_)
			ui chunkIndex{};
			ui slotIndex{};
	};
} // namespace Dimensia::ECS

#endif