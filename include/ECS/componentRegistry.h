/*! @file componentRegistry.h
	@brief Registry of component types and metadata used by the ECS.
	@details Declares the compile-time `ComponentTypes` tuple listing all concrete component and tag types used by the engine, compile-time
   utilities to map a component type to a stable ID, type traits to detect tag components, runtime component metadata in `ComponentInfo`,
   and size/alignment constants used for component storage and construction/destruction.
	@date 02/14/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMPONENTREGISTRY_H
#define INCLUDE_ECS_COMPONENTREGISTRY_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>

#include "Components/Buff/buffComponent.h"
#include "Components/Buffs/buffsComponent.h"
#include "Components/Health/healthComponent.h"
#include "Components/Mana/manaComponent.h"
#include "Components/Name/nameComponent.h"
#include "Components/Position/positionComponent.h"
#include "Components/Velocity/velocityComponent.h"
#include "Core/typedefs.h"
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
	/*! @brief Tuple listing all concrete component and tag types registered with the ECS.
		@details The order of types in this tuple defines the compile-time stable component ID returned by `componentID<T>()`. Add new
	   component or tag types here to expose them to the registry. Keep tags and components in the same tuple so IDs are contiguous.
	*/
	using ComponentTypes
		= std::tuple<Dimensia::Components::Position, Dimensia::Components::Velocity, Dimensia::Components::Health,
					 Dimensia::Components::Mana, Dimensia::Components::Buff, Dimensia::Components::Buffs, Dimensia::Components::Name,
					 Dimensia::Tags::AliveTag, Dimensia::Tags::DebugTag, Dimensia::Tags::BuffedTag>;

	/*! @brief Compile-time utility to find the zero-based index of a type in a tuple.
		@tparam T The type to find.
		@tparam Tuple The tuple type to search in.
		@note Produces a `std::integral_constant<std::size_t, N>` where N is the index of T.
	*/
	template <typename T, typename Tuple>
	struct tuple_index;

	template <typename T, typename... Us>
	struct tuple_index<T, std::tuple<T, Us...>> : std::integral_constant<std::size_t, 0>
	{};

	template <typename T, typename U, typename... Us>
	struct tuple_index<T, std::tuple<U, Us...>> : std::integral_constant<std::size_t, 1 + tuple_index<T, std::tuple<Us...>>::value>
	{};

	/*! @brief Returns a stable compile-time component ID for the given type.
		@tparam T The component or tag type to query.
		@return A zero-based component ID within `ComponentTypes`.
		@pre `T` must appear in `ComponentTypes`; otherwise compilation fails via static_assert.
		@note The ID is suitable for indexing arrays sized to `std::tuple_size_v<ComponentTypes>`.
	*/
	template <typename T>
	constexpr std::size_t componentID() noexcept
	{
		static_assert(tuple_index<T, ComponentTypes>::value < std::tuple_size_v<ComponentTypes>,
					  "Component type not found in ComponentTypes list");

		return tuple_index<T, ComponentTypes>::value;
	}

	/*! @brief Type trait that detects whether a type is a tag component.
		@tparam T The type to test.
		@note A type is considered a tag component if it exposes a static boolean member named `is_tag`.
		@return Inherits from `std::true_type` when `T::is_tag` exists and is true, otherwise `std::false_type`.
	*/
	template <typename T, typename = void>
	struct is_tag_component : std::false_type
	{};

	template <typename T>
	struct is_tag_component<T, std::void_t<decltype(T::is_tag)>> : std::integral_constant<bool, T::is_tag>
	{};

	/*! @brief Maximum number of distinct component types the registry supports.
		@note This is a conservative upper bound and is independent from `ComponentTypes` size.
	*/
	constexpr std::size_t MAX_COMPONENTS{128};

	/*! @brief Integer type used to represent component type IDs at runtime. */
	using ComponentTypeID = Dimensia::Core::ui;

	/*! @brief Integer type used for versioning component storage generations. */
	using VersionType = Dimensia::Core::ul;

	/*! @brief Function pointer called to destroy a component instance.
		@param[in] void* Pointer to the component object to be destroyed.
	*/
	using DestructorFunc = void (*)(void *);

	/*! @brief Function pointer used to copy-construct a component in pre-allocated storage.
		@param[out] dest Destination storage pointer.
		@param[in] src Source object pointer.
	*/
	using CopyConstructFunc = void (*)(void *dest, const void *src);

	/*! @brief Function pointer used to move-construct a component in pre-allocated storage.
		@param[out] dest Destination storage pointer.
		@param[in] src Source object pointer (moved-from).
	*/
	using MoveConstructFunc = void (*)(void *dest, void *src);

	/*! @brief Function pointer invoked to add a component to an entity via the ECS instance.
		@param[in] ECS* Pointer to the ECS instance performing the add.
		@param[in] Entity& The target entity to which the component will be added.
		@param[in] std::byte* Scratch or storage pointer used by the add function.
	*/
	using AddFunc = void (*)(Dimensia::ECS::ECS *, const Dimensia::ECS::Entity &, std::byte *);

	/*! @struct ComponentInfo
		@brief Runtime metadata describing how to manage a component type.
		@details `ComponentInfo` stores function pointers and size/alignment information required by the ECS when allocating, constructing,
	   copying, moving, and destroying component instances. Tag components can be represented with `isTag = true` and may require no storage
	   for their payload.
	*/
	struct ComponentInfo
	{
		public:
			/*! @brief Function used to destroy a component instance (invoked with pointer to object). */
			DestructorFunc destructor;

			/*! @brief Function used to copy-construct a component into provided storage. */
			CopyConstructFunc copyConstruct;

			/*! @brief Function used to move-construct a component into provided storage. */
			MoveConstructFunc moveConstruct;

			/*! @brief Function used by the ECS to add this component type to an entity. */
			AddFunc addFunc;

			/*! @brief Size in bytes required to store one instance of the component. */
			std::size_t size;

			/*! @brief Alignment requirement in bytes for the component type. */
			std::size_t alignment;

			/*! @brief True when the type is a tag (no payload) rather than a full component. */
			bool isTag;
	};

	/*! @brief Array of runtime `ComponentInfo` metadata corresponding to `ComponentTypes`.
		@note The array index maps 1:1 to the value returned by `componentID<T>()` for types in `ComponentTypes`.
	*/
	extern const std::array<ComponentInfo, std::tuple_size_v<ComponentTypes>> ComponentInfos;

	/*! @brief Helper metafunction that computes the maximum `sizeof` and `alignof` among a tuple of types.
		@tparam Tuple A `std::tuple` of types to inspect.
		@note `MAX_COMPONENT_SIZE` and `MAX_COMPONENT_ALIGN` are derived from this helper for the `ComponentTypes` tuple to provide storage
	   sizing guarantees.
	*/
	template <typename>
	struct MaxSizeHelper;

	template <typename... Ts>
	struct MaxSizeHelper<std::tuple<Ts...>>
	{
		public:
			static constexpr std::size_t size{std::max({sizeof(Ts)...})};
			static constexpr std::size_t alignment{std::max({alignof(Ts)...})};
	};

	/*! @brief Maximum size in bytes required to store any component listed in `ComponentTypes`. */
	constexpr std::size_t MAX_COMPONENT_SIZE = MaxSizeHelper<ComponentTypes>::size;

	/*! @brief Maximum alignment in bytes required by any component listed in `ComponentTypes`. */
	constexpr std::size_t MAX_COMPONENT_ALIGN{MaxSizeHelper<ComponentTypes>::alignment};
} // namespace Dimensia::Registry

#endif