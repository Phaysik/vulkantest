/*! \file componentMask.cpp
	\brief Contains the function definitions for creating a componentMask
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/componentMask.h"

#include "Core/typedefs.h"

namespace std
{
	std::size_t hash<Dimensia::ECS::ComponentMask>::operator()(const Dimensia::ECS::ComponentMask &componentMask) const noexcept
	{
		return hash<Dimensia::Core::ul>{}(componentMask.mLow) ^ (hash<Dimensia::Core::ul>{}(componentMask.mHigh) << 1U);
	}
} // namespace std
