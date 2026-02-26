/*! \file componentRegistry.cpp
	\brief Contains the function definitions for creating a componentRegistry
	\date 02/17/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/componentRegistry.h"

#include <array>
#include <tuple>

#include "ECS/componentRegistryImpl.h"

namespace Dimensia::Registry
{
	// NOLINTNEXTLINE(fuchsia-statically-constructed-objects)
	constexpr std::array<ComponentInfo, std::tuple_size_v<ComponentTypes>> ComponentInfos{makeComponentInfos(ComponentTypes{})};
} // namespace Dimensia::Registry