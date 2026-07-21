/*! @file componentList.h
	@brief Lightweight manifest of all registered component and tag types.
	@details This header defines the authoritative `ComponentTypes` type list and the compile-time `componentID<T>()` function.
   It uses only forward declarations — no full component headers are included. To register a new component or tag:
   1. Add a forward declaration in the appropriate namespace block below.
   2. Append the type to the `ComponentTypes` list (order defines the stable ID).
	@date 07/21/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMPONENTLIST_H
#define INCLUDE_ECS_COMPONENTLIST_H

#include <cstddef>
#include <tuple>
#include <type_traits>

// ============================================================================
// Forward declarations of all registered component and tag types.
// *** ADD NEW COMPONENT FORWARD DECLARATIONS HERE ***
// ============================================================================

namespace Dimensia::Components
{
	struct Position;
	struct Velocity;
	struct Health;
	struct Mana;
	struct Buff;
	struct Buffs;
	struct Name;
} // namespace Dimensia::Components

namespace Dimensia::Tags
{
	struct AliveTag;
	struct DebugTag;
	struct BuffedTag;
} // namespace Dimensia::Tags

namespace Dimensia::Registry
{
	/*! @brief Compile-time type list used as the component registry manifest.
		@tparam Ts Registered component and tag types.
		@note This is intentionally NOT `std::tuple` — it requires no complete types for the alias definition,
	   allowing `componentList.h` to remain free of full component header includes.
	*/
	template <typename...>
	struct ComponentTypeList
	{};

	// ============================================================================
	// The authoritative list of registered types. Order defines compile-time IDs.
	// *** ADD NEW TYPES TO THIS LIST ***
	// ============================================================================

	/*! @brief The ordered list of all component and tag types known to the ECS.
		@details The position of each type determines its compile-time `componentID`. Append new types at the end
	   to preserve existing IDs. Do not reorder or remove entries from the middle.
	*/
	using ComponentTypes
		= ComponentTypeList<Dimensia::Components::Position, Dimensia::Components::Velocity, Dimensia::Components::Health,
							Dimensia::Components::Mana, Dimensia::Components::Buff, Dimensia::Components::Buffs, Dimensia::Components::Name,
							Dimensia::Tags::AliveTag, Dimensia::Tags::DebugTag, Dimensia::Tags::BuffedTag>;

	// ============================================================================
	// Compile-time utilities
	// ============================================================================

	/*! @brief Computes the number of types in a `ComponentTypeList`.
		@tparam List A `ComponentTypeList<...>` instantiation.
	*/
	template <typename>
	struct ComponentTypeListSize;

	template <typename... Ts>
	struct ComponentTypeListSize<ComponentTypeList<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)>
	{};

	/*! @brief Total number of registered component and tag types. */
	constexpr std::size_t COMPONENT_COUNT{ComponentTypeListSize<ComponentTypes>::value};

	/*! @brief Compile-time utility to find the zero-based index of a type in a `ComponentTypeList`.
		@tparam T The type to find.
		@tparam List The list to search.
	*/
	template <typename T, typename List>
	struct type_index;

	template <typename T, typename... Us>
	struct type_index<T, ComponentTypeList<T, Us...>> : std::integral_constant<std::size_t, 0>
	{};

	template <typename T, typename U, typename... Us>
	struct type_index<T, ComponentTypeList<U, Us...>>
		: std::integral_constant<std::size_t, 1 + type_index<T, ComponentTypeList<Us...>>::value>
	{};

	/*! @brief Returns a stable compile-time component ID for the given type.
		@tparam T The component or tag type to query.
		@return A zero-based component ID within `ComponentTypes`.
		@pre `T` must appear in `ComponentTypes`; otherwise compilation fails via static_assert.
	*/
	template <typename T>
	constexpr std::size_t componentID() noexcept
	{
		static_assert(type_index<T, ComponentTypes>::value < COMPONENT_COUNT, "Component type not found in ComponentTypes list");

		return type_index<T, ComponentTypes>::value;
	}

	/*! @brief Converts a `ComponentTypeList<...>` to a `std::tuple<...>`.
		@details Used in implementation files that need complete types for metadata construction.
	*/
	template <typename>
	struct ToTuple;

	template <typename... Ts>
	struct ToTuple<ComponentTypeList<Ts...>>
	{
			using type = std::tuple<Ts...>;
	};

} // namespace Dimensia::Registry

#endif
