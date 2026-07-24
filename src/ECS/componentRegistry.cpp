/*! \file componentRegistry.cpp
	\brief Contains the function definitions for creating a componentRegistry
	\date 02/17/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/componentRegistry.h"

#include <array>

#include "ECS/componentList.h"
#include "ECS/componentRegistryImpl.h"

namespace Dimensia::Registry
{
	using ComponentTypesTuple = ToTuple<ComponentTypes>::type;

	// NOLINTNEXTLINE(fuchsia-statically-constructed-objects)
	constexpr std::array<ComponentInfo, COMPONENT_COUNT> ComponentInfos{makeComponentInfos(ComponentTypesTuple{})};
} // namespace Dimensia::Registry