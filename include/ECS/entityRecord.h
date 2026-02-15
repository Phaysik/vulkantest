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

// Forward declaration
class Archetype;

struct EntityRecord
{
		uint32_t generation;
		Archetype *archetype;
		uint32_t chunkIndex;
		uint32_t slotIndex;
};

#endif