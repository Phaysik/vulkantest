/*! \file componentMask.cpp
	\brief Contains the function definitions for creating a componentMask
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/componentMask.h"

namespace std
{
	size_t hash<Dimensia::ECS::ComponentMask>::operator()(const Dimensia::ECS::ComponentMask &m) const noexcept
	{
		return hash<uint64_t>{}(m.low) ^ (hash<uint64_t>{}(m.high) << 1);
	}
} // namespace std
