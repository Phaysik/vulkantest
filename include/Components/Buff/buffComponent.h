/*! \file buffComponent.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_COMPONENTS_BUFF_BUFFCOMPONENT_H
#define INCLUDE_COMPONENTS_BUFF_BUFFCOMPONENT_H

#include <string>

#include "Core/typedefs.h"

namespace Dimensia::Components
{
	struct Buff
	{
		public:
			std::string name;
			Dimensia::Core::si duration{};
	};
} // namespace Dimensia::Components

#endif