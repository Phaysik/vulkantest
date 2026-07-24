/*! @file componentRegistryImpl.h
	@brief Header-only implementation helpers for constructing component metadata.
	@details Provides the `makeComponentInfos` template used to build the compile-time `ComponentInfos` array from a `std::tuple` of
   component and tag types. The template generates `ComponentInfo` entries that include destructor, copy/move constructors, an add-function
   callable by the `Dimensia::ECS::ECS` instance, and the size/alignment requirements for each type.
	@note The actual definition of the `ComponentInfos` object is intended to live in a translation unit where `Dimensia::ECS::ECS` is a
   complete type. The template is kept in this header to remain ODR-visible for compile-time construction.
	@date 02/26/2026
*/

#ifndef INCLUDE_ECS_COMPONENTREGISTRYIMPL_H
#define INCLUDE_ECS_COMPONENTREGISTRYIMPL_H

#include <array>
#include <tuple>
#include <type_traits>

#include "Components/Buff/buffComponent.h"
#include "Components/Buffs/buffsComponent.h"
#include "Components/Health/healthComponent.h"
#include "Components/Mana/manaComponent.h"
#include "Components/Name/nameComponent.h"
#include "Components/Position/positionComponent.h"
#include "Components/Velocity/velocityComponent.h"
#include "ECS/componentRegistry.h"
#include "ECS/ecs.h"
#include "Tags/Alive/aliveTag.h"
#include "Tags/Buffed/buffedTag.h"
#include "Tags/Debug/debugTag.h"

namespace Dimensia::Registry
{
	/*! @brief Build a constexpr array of `ComponentInfo` entries for the given types.
		@tparam Ts Pack of component and tag types to describe.
		@param[in] componentInfos Unused parameter used only for template argument deduction; pass a `std::tuple<Ts...>`.
		@return A `std::array<ComponentInfo, sizeof...(Ts)>` where each element contains the destructor, copy/move construction functions,
	   an `addFunc` that forwards a value into `Dimensia::ECS::ECS::addComponent`, and size/alignment/isTag flags.
		@note The returned `ComponentInfo` entries capture behavior via function pointers that assume `Ts` is complete and that
	   `Dimensia::ECS::ECS::addComponent(entity, value)` is a valid operation for the given type. The `addFunc` constructs or obtains a `Ts`
	   instance from the provided `std::byte*` buffer using `std::launder` and then calls `ecs->addComponent` with the value (moved when
	   possible).
		@complexity Constant-time per component; the function executes at compile time when used in a `constexpr` context.
		@warning The implementation uses placement-new and `std::launder`; callers must ensure the buffer passed to `addFunc` is suitably
	   aligned and large enough (use `MAX_COMPONENT_SIZE` and `MAX_COMPONENT_ALIGN` from @ref Dimensia::Registry).
	*/
	template <typename... Ts>
	constexpr std::array<ComponentInfo, sizeof...(Ts)> makeComponentInfos(const std::tuple<Ts...> & /* componentInfos */)
	{
		static_assert((is_chunk_storable_component_v<Ts> && ...),
					  "Registered ECS components must fit one chunk row and must not require alignment greater than CHUNK_ALIGNMENT");
		static_assert((has_safe_chunk_lifecycle_v<Ts> && ...),
					  "Registered ECS components must be nothrow move constructible and nothrow destructible");

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
				  sizeof(Ts), alignof(Ts), is_tag_component<Ts>::value,}...},};
	}

} // namespace Dimensia::Registry

#endif
