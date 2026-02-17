/*! \file constants.h
	\brief Contains the constants for the ECS
	\date 02/17/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_CONSTANTS_H
#define INCLUDE_ECS_CONSTANTS_H
#include "Core/typedefs.h"

namespace Dimensia::ECS
{
	using Dimensia::Core::ul;

	constexpr ul UPPER_HALF_BIT_MASK{128};

	constexpr ul LOWER_HALF_BIT_MASK{64};
} // namespace Dimensia::ECS

#endif