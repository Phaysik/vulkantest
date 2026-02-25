// This header contains the `makeComponentInfos` template used to construct the
// compile-time `ComponentInfos` array. It is defined in a header so the template
// is ODR-visible, but the actual `ComponentInfos` definition remains in
// componentRegistry.cpp where `ECS` is complete.

#ifndef INCLUDE_ECS_COMPONENTREGISTRY_IMPL_H
#define INCLUDE_ECS_COMPONENTREGISTRY_IMPL_H

#include <array>
#include <tuple>
#include <type_traits>

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

} // namespace Dimensia::Registry

#endif
