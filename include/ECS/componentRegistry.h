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
#include "ECS/entity.h"
#include "Tags/Alive/aliveTag.h"
#include "Tags/Buffed/buffedTag.h"
#include "Tags/Debug/debugTag.h"

// Forward declaration of ECS
namespace Dimensia::ECS
{
	class ECS;
} // namespace Dimensia::ECS

namespace Dimensia::Registry
{
	// -----------------------------------------------------------------------------
	//  Compile‑time component registry
	// -----------------------------------------------------------------------------
	using ComponentTypes
		= std::tuple<Dimensia::Components::Position, Dimensia::Components::Velocity, Dimensia::Components::Health,
					 Dimensia::Components::Mana, Dimensia::Components::Buff, Dimensia::Components::Buffs, Dimensia::Components::Name,
					 Dimensia::Tags::AliveTag, Dimensia::Tags::DebugTag, Dimensia::Tags::BuffedTag>;

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

	// -----------------------------------------------------------------------------
	//  Configuration constants
	// -----------------------------------------------------------------------------
	constexpr std::size_t MAX_COMPONENTS{128};
	using ComponentTypeID = uint32_t;
	using VersionType = uint64_t;

	// -----------------------------------------------------------------------------
	//  ComponentInfo – type‑erased operations
	// -----------------------------------------------------------------------------

	using DestructorFunc = void (*)(void *);
	using CopyConstructFunc = void (*)(void *dest, const void *src);
	using MoveConstructFunc = void (*)(void *dest, void *src);
	using AddFunc = void (*)(Dimensia::ECS::ECS *, const Dimensia::ECS::Entity &, std::byte *);

	struct ComponentInfo
	{
		public:
			DestructorFunc destructor;
			CopyConstructFunc copyConstruct;
			MoveConstructFunc moveConstruct;
			AddFunc addFunc;
			std::size_t size;
			std::size_t alignment;
			bool isTag;
	};

	// Declaration only – definition in ComponentRegistry.cpp
	extern const std::array<ComponentInfo, std::tuple_size_v<ComponentTypes>> ComponentInfos;

	// Compute maximum component size and alignment at compile time
	template <typename>
	struct MaxSizeHelper;

	template <typename... Ts>
	struct MaxSizeHelper<std::tuple<Ts...>>
	{
		public:
			static constexpr std::size_t size{std::max({sizeof(Ts)...})};
			static constexpr std::size_t alignment{std::max({alignof(Ts)...})};
	};

	constexpr std::size_t MAX_COMPONENT_SIZE = MaxSizeHelper<ComponentTypes>::size;
	constexpr std::size_t MAX_COMPONENT_ALIGN{MaxSizeHelper<ComponentTypes>::alignment};
} // namespace Dimensia::Registry

#endif