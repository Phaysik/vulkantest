/*! \file componentRegistry.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMPONENTREGISTRY_H
#define INCLUDE_ECS_COMPONENTREGISTRY_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "Components/Buff/buffComponent.h"
#include "Components/Buffs/buffsComponent.h"
#include "Components/Health/healthComponent.h"
#include "Components/Mana/manaComponent.h"
#include "Components/Name/nameComponent.h"
#include "Components/Position/positionComponent.h"
#include "Components/Velocity/velocityComponent.h"
#include "Tags/Alive/aliveTag.h"
#include "Tags/Buffed/buffedTag.h"
#include "Tags/Debug/debugTag.h"

namespace Dimensia::Registry
{
	// -----------------------------------------------------------------------------
	//  Compile‑time component registry
	// -----------------------------------------------------------------------------
	using ComponentTypes = std::tuple<Components::Position, Components::Velocity, Components::Health, Components::Mana, Components::Buff,
									  Components::Buffs, Components::Name, Tags::AliveTag, Tags::DebugTag, Tags::BuffedTag>;

	// -----------------------------------------------------------------------------
	//  Type utilities
	// -----------------------------------------------------------------------------
	template <typename T, typename Tuple>
	struct tuple_index;

	template <typename T, typename... Us>
	struct tuple_index<T, std::tuple<T, Us...>> : std::integral_constant<std::size_t, 0>
	{};

	template <typename T, typename U, typename... Us>
	struct tuple_index<T, std::tuple<U, Us...>> : std::integral_constant<std::size_t, 1 + tuple_index<T, std::tuple<Us...>>::value>
	{};

	template <typename T>
	constexpr std::size_t componentId() noexcept
	{
		static_assert(tuple_index<T, ComponentTypes>::value < std::tuple_size_v<ComponentTypes>,
					  "Component type not found in ComponentTypes list");
		return tuple_index<T, ComponentTypes>::value;
	}

	// -----------------------------------------------------------------------------
	//  Tag detection
	// -----------------------------------------------------------------------------
	template <typename T, typename = void>
	struct is_tag_component : std::false_type
	{};

	template <typename T>
	struct is_tag_component<T, std::void_t<decltype(T::is_tag)>> : std::integral_constant<bool, T::is_tag>
	{};

	template <typename>
	struct always_false : std::false_type
	{};

	// -----------------------------------------------------------------------------
	//  Configuration constants
	// -----------------------------------------------------------------------------
	constexpr std::size_t MAX_COMPONENTS = 128;
	using ComponentTypeID = uint32_t;
	using VersionType = uint64_t;

	// -----------------------------------------------------------------------------
	//  ComponentInfo – type‑erased operations
	// -----------------------------------------------------------------------------
	struct ComponentInfo
	{
			std::size_t size;
			std::size_t alignment;
			void (*destructor)(void *);
			void (*copyConstruct)(void *dest, const void *src);
			void (*moveConstruct)(void *dest, void *src);
			bool isTag;
	};

	// Build a constexpr array of ComponentInfo from the type list
	template <typename... Ts>
	constexpr std::array<ComponentInfo, sizeof...(Ts)> make_component_infos(std::tuple<Ts...>)
	{
		return {{{sizeof(Ts), alignof(Ts), [](void *ptr) { static_cast<Ts *>(ptr)->~Ts(); },
				  [](void *dest, const void *src) { new (dest) Ts(*static_cast<const Ts *>(src)); },
				  [](void *dest, void *src) {
					  if constexpr (std::is_move_constructible_v<Ts>)
					  {
						  new (dest) Ts(std::move(*static_cast<Ts *>(src)));
					  }
					  else if constexpr (std::is_copy_constructible_v<Ts>)
					  {
						  new (dest) Ts(*static_cast<const Ts *>(src));
					  }
					  else
					  {
						  static_assert(always_false<Ts>::value, "Component must be copy or move constructible");
					  }
				  },
				  is_tag_component<Ts>::value}...}};
	}

	// Global compile‑time info array (indexed by componentId)
	constexpr std::array<ComponentInfo, std::tuple_size_v<ComponentTypes>> ComponentInfos{make_component_infos(ComponentTypes{})};

	// Compute maximum component size and alignment at compile time
	template <typename>
	struct MaxSizeHelper;

	template <typename... Ts>
	struct MaxSizeHelper<std::tuple<Ts...>>
	{
			static constexpr std::size_t size = std::max({sizeof(Ts)...});
			static constexpr std::size_t alignment = std::max({alignof(Ts)...});
	};

	constexpr std::size_t MAX_COMPONENT_SIZE = MaxSizeHelper<ComponentTypes>::size;
	constexpr std::size_t MAX_COMPONENT_ALIGN = MaxSizeHelper<ComponentTypes>::alignment;
} // namespace Dimensia::Registry

#endif