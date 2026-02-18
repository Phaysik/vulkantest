/*! \file componentRegistry.cpp
	\brief Contains the function definitions for creating a componentRegistry
	\date 02/17/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/componentRegistry.h"

#include "ECS/ecs.h"

namespace Dimensia::Registry
{
	template <typename... Ts>
	// NOLINTNEXTLINE(misc-use-internal-linkage)
	constexpr std::array<ComponentInfo, sizeof...(Ts)> makeComponentInfos(const std::tuple<Ts...> & /* componentInfos */)
	{
		return {{{[](void *ptr) { static_cast<Ts *>(ptr)->~Ts(); },
				  [](void *dest, const void *src) { new (dest) Ts(*static_cast<const Ts *>(src)); },
				  [](void *dest, void *src) {
					  if constexpr (std::is_move_constructible_v<Ts>)
					  {
						  new (dest) Ts(std::move(*static_cast<Ts *>(src)));
					  }
					  else
					  {
						  new (dest) Ts(*static_cast<const Ts *>(src));
					  }
				  },
				  [](Dimensia::ECS::ECS *ecs, const Dimensia::ECS::Entity &entity, std::byte *buffer) {
					  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
					  Ts &value = *std::launder(reinterpret_cast<Ts *>(buffer));
					  ecs->addComponent(entity, std::move(value));
				  },
				  sizeof(Ts), alignof(Ts), is_tag_component<Ts>::value}...}};
	}

	// NOLINTNEXTLINE(fuchsia-statically-constructed-objects)
	constexpr std::array<ComponentInfo, std::tuple_size_v<ComponentTypes>> ComponentInfos{makeComponentInfos(ComponentTypes{})};
} // namespace Dimensia::Registry