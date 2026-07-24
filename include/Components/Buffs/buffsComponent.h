/*! \file buffsComponent.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_COMPONENTS_BUFFS_BUFFSCOMPONENT_H
#define INCLUDE_COMPONENTS_BUFFS_BUFFSCOMPONENT_H

#include <vector>

#include "Components/Buff/buffComponent.h"

namespace Dimensia::Components
{
	struct Buffs
	{
		public:
			std::vector<Buff> activeBuffs{};
	};
} // namespace Dimensia::Components

#endif