/*! @file componentRegistry.h
	@brief Registry of component types and metadata used by the ECS.
	@details Provides runtime component metadata (`ComponentInfo`), the `is_tag_component` trait, and the `ComponentInfos` extern array.
   The compile-time type list and `componentID<T>()` are defined in `componentList.h` which this header includes. Full component headers
   are NOT included here — they are only needed in `componentRegistryImpl.h` where metadata is constructed.
	@date 02/14/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMPONENTREGISTRY_H
#define INCLUDE_ECS_COMPONENTREGISTRY_H

#include <array>
#include <cstddef>
#include <type_traits>

#include "Core/typedefs.h"
#include "ECS/componentList.h"
#include "ECS/constants.h"
#include "ECS/entity.h"

// Forward declaration of ECS
namespace Dimensia::ECS
{
	class ECS;
} // namespace Dimensia::ECS

namespace Dimensia::Registry
{
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

	/*! @brief Indicates whether a component type can be stored in one ECS chunk row.
		@details Tags require no component column. Regular components must not exceed the chunk's base alignment and must leave enough room
	   for one entity handle, one tag mask, alignment padding, and one component instance.
		@tparam T Component or tag type to validate.
	*/
	template <typename T>
	constexpr bool is_chunk_storable_component_v = [] {
		if constexpr (is_tag_component<T>::value)
		{
			return true;
		}
		else
		{
			if constexpr (alignof(T) > Dimensia::ECS::CHUNK_ALIGNMENT)
			{
				return false;
			}

			constexpr std::size_t tagOffset{
				(sizeof(Dimensia::ECS::Entity) + alignof(Dimensia::Core::ul) - 1) & ~(alignof(Dimensia::Core::ul) - 1),
			};
			constexpr std::size_t componentStart{tagOffset + (sizeof(Dimensia::Core::ul) * Dimensia::ECS::TAG_WORDS_PER_ENTITY)};
			constexpr std::size_t componentOffset{(componentStart + alignof(T) - 1) & ~(alignof(T) - 1)};

			return componentOffset <= Dimensia::ECS::CHUNK_SIZE && sizeof(T) <= (Dimensia::ECS::CHUNK_SIZE - componentOffset);
		}
	}();

	/*! @brief Indicates whether a component's destructive relocation operations are safe for dense chunk storage.
		@tparam T Component or tag type to validate.
		@note Tags are accepted because they have no stored object lifetime. Regular components require no-throw move construction and
	   destruction so swap removal cannot leave a partially relocated row.
	*/
	template <typename T>
	constexpr bool has_safe_chunk_lifecycle_v
		= is_tag_component<T>::value || (std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>);

	/*! @brief Maximum number of distinct component types the registry supports.
		@note This is a conservative upper bound and is independent from `ComponentTypes` size.
	*/
	inline constexpr std::size_t MAX_COMPONENTS{128};

	/*! @brief Integer type used to represent component type IDs at runtime. */
	using ComponentTypeID = Dimensia::Core::ui;

	/*! @brief Integer type used for versioning component storage generations. */
	using VersionType = Dimensia::Core::ul;

	/*! @brief Function pointer called to destroy a component instance. */
	using DestructorFunc = void (*)(void *);

	/*! @brief Function pointer used to copy-construct a component in pre-allocated storage. */
	using CopyConstructFunc = void (*)(void *dest, const void *src);

	/*! @brief Function pointer used to move-construct a component in pre-allocated storage. */
	using MoveConstructFunc = void (*)(void *dest, void *src);

	/*! @brief Function pointer invoked to add a component to an entity via the ECS instance. */
	using AddFunc = void (*)(Dimensia::ECS::ECS *, const Dimensia::ECS::Entity &, std::byte *);

	/*! @struct ComponentInfo
		@brief Runtime metadata describing how to manage a component type.
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
		@note The array index maps 1:1 to the value returned by `componentID<T>()`.
	*/
	extern const std::array<ComponentInfo, COMPONENT_COUNT> ComponentInfos;

} // namespace Dimensia::Registry

#endif