/*! @file processChunkHelpers.h
	@brief Optimized helpers for processing entities within archetype chunks.
	@details This header contains small, performance-oriented utilities used when iterating entities stored in archetype chunks. It provides
   bit-iteration helpers, compile-time mask builders for tag and non-tag components, and templated chunk processors that call user-provided
   functions with correctly-typed component pointers. The APIs are designed for hot paths and avoid allocations; callers are responsible for
   synchronization when accessing archetypes from multiple threads.
	@date 02/14/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_PROCESSCHUNKHELPERS_H
#define INCLUDE_ECS_PROCESSCHUNKHELPERS_H

#include <bit>
#include <type_traits>
#include <utility>

#include "Core/attributeMacros.h"
#include "Core/cconcepts.h"
#include "Core/typedefs.h"

#include "archetype.h"
#include "componentMask.h"
#include "componentRegistry.h"
#include "constants.h"

namespace Dimensia::ECS
{
	using Dimensia::Registry::componentID;
	using Dimensia::Registry::ComponentInfos;
	using Dimensia::Registry::ComponentTypeID;
	using Dimensia::Registry::is_tag_component;

	using Dimensia::Core::si;
	using Dimensia::Core::ui;
	using Dimensia::Core::ul;

	/*! @brief Trait: true when `T` is a tag component.
		@tparam T Type to test for tag semantics.
	*/
	template <typename T>
	constexpr bool isTagV{is_tag_component<T>::value};

	/*! @brief Trait: true when any of `Components...` is a tag component.
		@tparam Components Parameter pack of component types to inspect.
	*/
	template <typename... Components>
	constexpr bool hasTagsV{(isTagV<Components> || ...)};

	/*! @brief Build a component mask containing non-tag component bits from `Components...`.
		@tparam Components Component types to examine; tag components are ignored.
		@note Intended for constructing required component masks at compile time.
		@return A `ComponentMask` with bits set for non-tag components present in `Components...`.
	*/
	template <typename... Components>
	constexpr ComponentMask buildRequiredMask() noexcept
	{
		ComponentMask mask{0, 0};
		(([&] {
			 if constexpr (!isTagV<Components>)
			 {
				 ComponentTypeID componentTypeID{componentID<Components>()};
				 if (componentTypeID < LOWER_HALF_BIT_MASK)
				 {
					 mask.mLow |= (1U << componentTypeID);
				 }
				 else
				 {
					 mask.mHigh |= (1U << (componentTypeID - LOWER_HALF_BIT_MASK));
				 }
			 }
		 }()),
		 ...);
		return mask;
	}

	/*! @brief Build a component mask containing only tag component bits from `Components...`.
		@tparam Components Component types to examine; only tag components contribute to the mask.
		@return A `ComponentMask` with bits set for tag components present in `Components...`.
		@note This is a constexpr helper used to construct required tag masks at compile time.
	*/
	template <typename... Components>
	constexpr ComponentMask buildTagMask() noexcept
	{
		ComponentMask mask{0, 0};
		(([&] {
			 if constexpr (isTagV<Components>)
			 {
				 ComponentTypeID componentTypeID{componentID<Components>()};
				 if (componentTypeID < LOWER_HALF_BIT_MASK)
				 {
					 mask.mLow |= (1U << componentTypeID);
				 }
				 else
				 {
					 mask.mHigh |= (1U << (componentTypeID - LOWER_HALF_BIT_MASK));
				 }
			 }
		 }()),
		 ...);
		return mask;
	}

	/*! @brief Iterate over all set bits in `mask` and invoke `func` with each bit's index.
		@tparam Func Callable type; must satisfy @ref Dimensia::Core::InvocableWithArgs "InvocableWithArgs<Func, ComponentTypeID>".
		@param[in] mask The `ComponentMask` to scan.
		@param[in] func Callable invoked once per set bit with the corresponding `ComponentTypeID`.
		@note Iteration order is from lower to higher indices within each half of the mask.
		@note O(number of set bits)
	*/
	template <typename Func>
		requires Dimensia::Core::InvocableWithArgs<Func, ComponentTypeID>
	constexpr void forEachSetBit(const ComponentMask &mask, Func &&func) noexcept
	{
		ul bits{mask.mLow};
		const Func forwardedFunction{std::forward<Func>(func)};

		while (bits)
		{
			const ul temp{bits & -bits};
			const si index{std::countr_zero(bits)};

			forwardedFunction(static_cast<ComponentTypeID>(index));
			bits ^= temp;
		}

		bits = mask.mHigh;
		while (bits)
		{
			const ul temp{bits & -bits};
			const si index{std::countr_zero(bits) + static_cast<si>(LOWER_HALF_BIT_MASK)};

			forwardedFunction(static_cast<ComponentTypeID>(index));
			bits ^= temp;
		}
	}

	/*! @brief Pointer alias for `Archetype` respecting constness.
		@tparam IsConst When true, yields `const Archetype *`, otherwise `Archetype *`.
	*/
	template <bool IsConst>
	using ArchPtr = std::conditional_t<IsConst, const Archetype *, Archetype *>;

	/*! @brief Pointer alias for `Entity` respecting constness.
		@tparam IsConst When true, yields `const Entity *`, otherwise `Entity *`.
	*/
	template <bool IsConst>
	using EntityPtr = std::conditional_t<IsConst, const Entity *, Entity *>;

	/*! @brief Process entities in a chunk when some `Components...` include tags.
		@tparam IsConst Compile-time constness for pointers (true -> const pointers passed to @p func).
		@tparam Components Component types to fetch and pass to @p func. Tag components are used for per-entity filtering.
		@tparam Func Callable type; invoked as `func(Entity, Components*...)` where pointer constness follows `IsConst`.
		@param[in] entityArr Pointer to the chunk's entity array.
		@param[in] entityCount Number of entities to process in the chunk.
		@param[in] chunkIndex Index of the chunk within the archetype.
		@param[in] arch Pointer to the archetype containing component arrays and tag bitsets.
		@param[in] func Callable invoked for each entity that satisfies the required tag mask.
		@param[in] indexSequence Index sequence used to advance typed component byte pointers (internal).
		@note This function inspects per-entity tag bits and skips entities that do not match all required tags.
		@note Uses pointer arithmetic and reinterpret_cast to obtain typed component pointers; intended for hot paths.
	*/
	template <bool IsConst, typename... Components, typename Func, std::size_t... Is>
	void processChunkEntitiesWithTags(EntityPtr<IsConst> entityArr, const ui entityCount, const ui chunkIndex, ArchPtr<IsConst> arch,
									  Func &&func, const std::index_sequence<Is...> & /* indexSequence*/)
	{
		using BytePtr = std::conditional_t<IsConst, const std::byte *, std::byte *>;

		const ul *tagBits{arch->getTagBitset(chunkIndex)};
		const ComponentMask requiredTags{buildTagMask<Components...>()};

		// Store pointers as std::byte* in a tuple with deduced type
		auto byteArrays{std::tuple{static_cast<BytePtr>(arch->getComponentArray(chunkIndex, componentID<Components>()))...}};

		const Func forwardedFunction{std::forward<Func>(func)};

		for (ui slot{0}; slot < entityCount; ++slot)
		{
			assert(slot < entityCount);
			assert(slot < arch->getEntityCount(chunkIndex));

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)

			Entity entity{entityArr[slot]};
			const ComponentMask entityTags{tagBits[slot * TAG_WORDS_PER_ENTITY], tagBits[(slot * TAG_WORDS_PER_ENTITY) + 1]};

			// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

			if ((entityTags & requiredTags) != requiredTags)
			{
				continue;
			}

			// Call user function with correctly typed pointers
			std::apply(
				[&](auto *...bytePtrs) noexcept {
					forwardedFunction(
						// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
						entity, (*reinterpret_cast<std::conditional_t<IsConst, const Components *, Components *>>(bytePtrs))...);
				},
				byteArrays);

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			((std::get<Is>(byteArrays) += ComponentInfos[componentID<Components>()].size), ...);
		}
	}

	/*! @brief Process entities in a chunk when `Components...` contain no tag components.
		@tparam IsConst Compile-time constness for pointers passed to @p func.
		@tparam Components Component types to fetch and pass to @p func (no tag components expected).
		@tparam Func Callable type; invoked as `func(Entity, Components*...)`.
		@param[in] entityArr Pointer to the chunk's entity array.
		@param[in] entityCount Number of entities to process in the chunk.
		@param[in] chunkIndex Index of the chunk within the archetype.
		@param[in] arch Pointer to the archetype containing component arrays.
		@param[in] func Callable invoked for each entity.
		@param[in] indexSequence Index sequence used to advance typed component byte pointers (internal).
		@note Avoids tag checks for marginally faster iteration when tags are not involved.
	*/
	template <bool IsConst, typename... Components, typename Func, std::size_t... Is>
	void processChunkEntitiesNoTags(EntityPtr<IsConst> entityArr, ui entityCount, ATTR_MAYBE_UNUSED ui chunkIndex,
									ATTR_MAYBE_UNUSED ArchPtr<IsConst> arch, Func &&func, std::index_sequence<Is...> /* indexSequence*/)
	{
		using BytePtr = std::conditional_t<IsConst, const std::byte *, std::byte *>;

		auto byteArrays{std::tuple{static_cast<BytePtr>(arch->getComponentArray(chunkIndex, componentID<Components>()))...}};

		const Func forwardedFunction{std::forward<Func>(func)};

		for (ui slot{0}; slot < entityCount; ++slot)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			Entity entity{entityArr[slot]};

			std::apply(
				[&](auto *...bytePtrs) noexcept {
					forwardedFunction(
						// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
						entity, (*reinterpret_cast<std::conditional_t<IsConst, const Components *, Components *>>(bytePtrs))...);
				},
				byteArrays);

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			((std::get<Is>(byteArrays) += ComponentInfos[componentID<Components>()].size), ...);
		}
	}

	/*! @brief Dispatch helper that processes entities with non-const pointers.
		@tparam Components Component types to fetch and pass to @p func.
		@tparam Func Callable type; invoked as `func(Entity, Components*...)`.
		@param[in] entityArr Pointer to the chunk's entity array.
		@param[in] entityCount Number of entities to process.
		@param[in] chunkIndex Chunk index within the archetype.
		@param[in] arch Pointer to the archetype.
		@param[in] func Callable invoked for each entity.
		@note Selects the tag-aware or tag-free implementation depending on whether any `Components` are tags.
	*/
	template <typename... Components, typename Func>
	void processChunkEntities(Entity *entityArr, ui entityCount, ui chunkIndex, Archetype *arch, Func &&func)
	{
		if constexpr (hasTagsV<Components...>)
		{
			processChunkEntitiesWithTags<false, Components...>(entityArr, entityCount, chunkIndex, arch, std::forward<Func>(func),
															   std::index_sequence_for<Components...>{});
		}
		else
		{
			processChunkEntitiesNoTags<false, Components...>(entityArr, entityCount, chunkIndex, arch, std::forward<Func>(func),
															 std::index_sequence_for<Components...>{});
		}
	}

	/*! @brief Dispatch helper that processes entities with const pointers.
		@tparam Components Component types to fetch and pass to @p func.
		@tparam Func Callable type; invoked as `func(Entity, const Components*...)`.
		@param[in] entityArr Pointer to the chunk's entity array (const-qualified).
		@param[in] entityCount Number of entities to process.
		@param[in] chunkIndex Chunk index within the archetype.
		@param[in] arch Const pointer to the archetype.
		@param[in] func Callable invoked for each entity; receives const component pointers.
	*/
	template <typename... Components, typename Func>
	void processChunkEntitiesConst(const Entity *entityArr, ui entityCount, ui chunkIndex, const Archetype *arch, Func &&func)
	{
		if constexpr (hasTagsV<Components...>)
		{
			processChunkEntitiesWithTags<true, Components...>(entityArr, entityCount, chunkIndex, arch, std::forward<Func>(func),
															  std::index_sequence_for<Components...>{});
		}
		else
		{
			processChunkEntitiesNoTags<true, Components...>(entityArr, entityCount, chunkIndex, arch, std::forward<Func>(func),
															std::index_sequence_for<Components...>{});
		}
	}
} // namespace Dimensia::ECS

#endif