/*! \file processChunkHelpers.h
	\brief Contains optimized chunk processing helpers
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_PROCESSCHUNKHELPERS_H
#define INCLUDE_ECS_PROCESSCHUNKHELPERS_H

#include <bit>
#include <type_traits>
#include <utility>

#include "Core/typedefs.h"

#include "archetype.h"
#include "componentMask.h"
#include "componentRegistry.h"
#include "constants.h"

namespace Dimensia::ECS
{
	using Dimensia::Registry::componentId;
	using Dimensia::Registry::ComponentInfos;
	using Dimensia::Registry::ComponentTypeID;
	using Dimensia::Registry::is_tag_component;

	using Dimensia::Core::si;
	using Dimensia::Core::ui;
	using Dimensia::Core::ul;

	// ------------------------------------------------------------------------
	//  Tag detection helpers
	// ------------------------------------------------------------------------
	template <typename T>
	constexpr bool is_tag_v{is_tag_component<T>::value};

	template <typename... Components>
	constexpr bool has_tags_v{(is_tag_v<Components> || ...)};

	template <typename... Components>
	constexpr ComponentMask build_tag_mask() noexcept
	{
		ComponentMask mask{0, 0};
		(([&] {
			 if constexpr (is_tag_v<Components>)
			 {
				 ComponentTypeID componentTypeID{componentId<Components>()};
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

	// Helper to iterate over set bits in a ComponentMask
	template <typename Func>
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

	// Build required mask for a set of component types (non‑tags only)
	template <typename... Components>
	constexpr ComponentMask build_required_mask() noexcept
	{
		ComponentMask mask{0, 0};
		(([&] {
			 if constexpr (!is_tag_v<Components>)
			 {
				 ComponentTypeID componentTypeID{componentId<Components>()};
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

	template <bool IsConst>
	using ArchPtr = std::conditional_t<IsConst, const Archetype *, Archetype *>;

	template <bool IsConst>
	using EntityPtr = std::conditional_t<IsConst, const Entity *, Entity *>;

	// ------------------------------------------------------------------------
	//  Process chunk with tag checks (used when has_tags_v is true)
	// ------------------------------------------------------------------------
	template <bool IsConst, typename... Components, typename Func, std::size_t... Is>
	void processChunkEntitiesWithTags(EntityPtr<IsConst> entityArr, const ui entityCount, const ui chunkIndex, ArchPtr<IsConst> arch,
									  Func &&func, const std::index_sequence<Is...> & /* indexSequence */)
	{
		using BytePtr = std::conditional_t<IsConst, const std::byte *, std::byte *>;

		const ul *tagBits{arch->getTagBitset(chunkIndex)};
		const ComponentMask requiredTags{build_tag_mask<Components...>()};

		// Store pointers as std::byte* in a tuple with deduced type
		auto byteArrays{std::tuple{static_cast<BytePtr>(arch->getComponentArray(chunkIndex, componentId<Components>()))...}};

		const Func forwardedFunction{std::forward<Func>(func)};

		for (ui slot{0}; slot < entityCount; ++slot)
		{
			assert(slot < entityCount);
			assert(slot < arch->getEntityCount(chunkIndex));

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)

			Entity entity{entityArr[slot]};
			const ComponentMask entityTags{tagBits[slot], 0};

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
			((std::get<Is>(byteArrays) += ComponentInfos[componentId<Components>()].size), ...);
		}
	}

	// ------------------------------------------------------------------------
	//  Process chunk without tag checks (used when has_tags_v is false)
	// ------------------------------------------------------------------------
	template <bool IsConst, typename... Components, typename Func, std::size_t... Is>
	void processChunkEntitiesNoTags(EntityPtr<IsConst> entityArr, ui entityCount, ui chunkIndex, ArchPtr<IsConst> arch, Func &&func,
									std::index_sequence<Is...> /* indexSequence */)
	{
		using BytePtr = std::conditional_t<IsConst, const std::byte *, std::byte *>;

		auto byteArrays{std::tuple{static_cast<BytePtr>(arch->getComponentArray(chunkIndex, componentId<Components>()))...}};

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
			((std::get<Is>(byteArrays) += ComponentInfos[componentId<Components>()].size), ...);
		}
	}

	// ------------------------------------------------------------------------
	//  Public dispatch for non‑const version
	// ------------------------------------------------------------------------
	template <typename... Components, typename Func>
	void processChunkEntities(Entity *entityArr, ui entityCount, ui chunkIndex, Archetype *arch, Func &&func)
	{
		if constexpr (has_tags_v<Components...>)
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

	// ------------------------------------------------------------------------
	//  Public dispatch for const version
	// ------------------------------------------------------------------------
	template <typename... Components, typename Func>
	void processChunkEntitiesConst(const Entity *entityArr, ui entityCount, ui chunkIndex, const Archetype *arch, Func &&func)
	{
		if constexpr (has_tags_v<Components...>)
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