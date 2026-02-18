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
	constexpr bool is_tag_v = is_tag_component<T>::value;

	template <typename... Components>
	constexpr bool has_tags_v = (is_tag_v<Components> || ...);

	template <typename... Components>
	constexpr ComponentMask build_tag_mask()
	{
		ComponentMask mask{0, 0};
		(([&] {
			 if constexpr (is_tag_v<Components>)
			 {
				 ComponentTypeID id = componentId<Components>();
				 if (id < LOWER_HALF_BIT_MASK)
				 {
					 mask.mLow |= (1U << id);
				 }
				 else
				 {
					 mask.mHigh |= (1U << (id - LOWER_HALF_BIT_MASK));
				 }
			 }
		 }()),
		 ...);
		return mask;
	}

	// Helper to iterate over set bits in a ComponentMask
	template <typename F>
	constexpr void forEachSetBit(const ComponentMask &mask, F &&func) noexcept
	{
		ul bits{mask.mLow};
		while (bits)
		{
			const ul temp{bits & -bits};
			const si idx{std::countr_zero(bits)};

			std::forward<F>(func)(static_cast<ComponentTypeID>(idx));
			bits ^= temp;
		}

		bits = mask.mHigh;
		while (bits)
		{
			const ul temp{bits & -bits};
			const si idx{std::countr_zero(bits) + static_cast<si>(LOWER_HALF_BIT_MASK)};

			std::forward<F>(func)(static_cast<ComponentTypeID>(idx));
			bits ^= temp;
		}
	}

	// Build required mask for a set of component types (non‑tags only)
	template <typename... Components>
	constexpr ComponentMask build_required_mask()
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

	// ------------------------------------------------------------------------
	//  Process chunk with tag checks (used when has_tags_v is true)
	// ------------------------------------------------------------------------
	template <typename... Components, typename Func, std::size_t... Is>
	void process_chunk_entities_with_tags_impl(Entity *entityArr, ui entityCount, ui chunkIdx, Archetype *arch, Func &&func,
											   std::index_sequence<Is...>)
	{
		const ul *tagBits = arch->getTagBitset(chunkIdx);
		const ComponentMask requiredTags = build_tag_mask<Components...>();

		// Store pointers as std::byte* in a tuple with deduced type
		auto byteArrays = std::tuple{static_cast<std::byte *>(arch->getComponentArray(chunkIdx, componentId<Components>()))...};

		for (ui slot = 0; slot < entityCount; ++slot)
		{
			Entity entity = entityArr[slot];
			ComponentMask entityTags{tagBits[slot], 0};
			if ((entityTags & requiredTags) != requiredTags)
			{
				continue;
			}

			// Call user function with correctly typed pointers
			std::apply([&](auto *...bytePtrs) noexcept { func(entity, (*reinterpret_cast<Components *>(bytePtrs))...); }, byteArrays);

			// Advance each pointer by component size
			((std::get<Is>(byteArrays) += ComponentInfos[componentId<Components>()].size), ...);
		}
	}

	template <typename... Components, typename Func>
	void process_chunk_entities_with_tags(Entity *entityArr, ui entityCount, ui chunkIdx, Archetype *arch, Func &&func)
	{
		process_chunk_entities_with_tags_impl<Components...>(entityArr, entityCount, chunkIdx, arch, std::forward<Func>(func),
															 std::index_sequence_for<Components...>{});
	}

	// ------------------------------------------------------------------------
	//  Process chunk without tag checks (used when has_tags_v is false)
	// ------------------------------------------------------------------------
	template <typename... Components, typename Func, std::size_t... Is>
	void process_chunk_entities_no_tags_impl(Entity *entityArr, ui entityCount, ui chunkIdx, Archetype *arch, Func &&func,
											 std::index_sequence<Is...>)
	{
		auto byteArrays = std::tuple{static_cast<std::byte *>(arch->getComponentArray(chunkIdx, componentId<Components>()))...};

		for (ui slot = 0; slot < entityCount; ++slot)
		{
			Entity entity = entityArr[slot];

			std::apply([&](auto *...bytePtrs) noexcept { func(entity, (*reinterpret_cast<Components *>(bytePtrs))...); }, byteArrays);

			((std::get<Is>(byteArrays) += ComponentInfos[componentId<Components>()].size), ...);
		}
	}

	template <typename... Components, typename Func>
	void process_chunk_entities_no_tags(Entity *entityArr, ui entityCount, ui chunkIdx, Archetype *arch, Func &&func)
	{
		process_chunk_entities_no_tags_impl<Components...>(entityArr, entityCount, chunkIdx, arch, std::forward<Func>(func),
														   std::index_sequence_for<Components...>{});
	}

	// ------------------------------------------------------------------------
	//  Public dispatch for non‑const version
	// ------------------------------------------------------------------------
	template <typename... Components, typename Func>
	void process_chunk_entities(Entity *entityArr, ui entityCount, ui chunkIdx, Archetype *arch, Func &&func)
	{
		if constexpr (has_tags_v<Components...>)
		{
			process_chunk_entities_with_tags<Components...>(entityArr, entityCount, chunkIdx, arch, std::forward<Func>(func));
		}
		else
		{
			process_chunk_entities_no_tags<Components...>(entityArr, entityCount, chunkIdx, arch, std::forward<Func>(func));
		}
	}

	// ------------------------------------------------------------------------
	//  Const versions – use const std::byte* pointers
	// ------------------------------------------------------------------------
	template <typename... Components, typename Func, std::size_t... Is>
	void process_chunk_entities_with_tags_const_impl(const Entity *entityArr, ui entityCount, ui chunkIdx, const Archetype *arch,
													 Func &&func, std::index_sequence<Is...>)
	{
		const ul *tagBits = arch->getTagBitset(chunkIdx);
		const ComponentMask requiredTags = build_tag_mask<Components...>();

		auto byteArrays = std::tuple{static_cast<const std::byte *>(arch->getComponentArray(chunkIdx, componentId<Components>()))...};

		for (ui slot = 0; slot < entityCount; ++slot)
		{
			Entity entity = entityArr[slot];
			ComponentMask entityTags{tagBits[slot], 0};
			if ((entityTags & requiredTags) != requiredTags)
			{
				continue;
			}

			std::apply([&](auto *...bytePtrs) noexcept { func(entity, (*reinterpret_cast<const Components *>(bytePtrs))...); }, byteArrays);

			// Advance each pointer by component size
			((const_cast<const std::byte *&>(std::get<Is>(byteArrays)) += ComponentInfos[componentId<Components>()].size), ...);
		}
	}

	template <typename... Components, typename Func>
	void process_chunk_entities_with_tags_const(const Entity *entityArr, ui entityCount, ui chunkIdx, const Archetype *arch, Func &&func)
	{
		process_chunk_entities_with_tags_const_impl<Components...>(entityArr, entityCount, chunkIdx, arch, std::forward<Func>(func),
																   std::index_sequence_for<Components...>{});
	}

	template <typename... Components, typename Func, std::size_t... Is>
	void process_chunk_entities_no_tags_const_impl(const Entity *entityArr, ui entityCount, ui chunkIdx, const Archetype *arch, Func &&func,
												   std::index_sequence<Is...>)
	{
		auto byteArrays = std::tuple{static_cast<const std::byte *>(arch->getComponentArray(chunkIdx, componentId<Components>()))...};

		for (ui slot = 0; slot < entityCount; ++slot)
		{
			Entity entity = entityArr[slot];

			std::apply([&](auto *...bytePtrs) noexcept { func(entity, (*reinterpret_cast<const Components *>(bytePtrs))...); }, byteArrays);

			((const_cast<const std::byte *&>(std::get<Is>(byteArrays)) += ComponentInfos[componentId<Components>()].size), ...);
		}
	}

	template <typename... Components, typename Func>
	void process_chunk_entities_no_tags_const(const Entity *entityArr, ui entityCount, ui chunkIdx, const Archetype *arch, Func &&func)
	{
		process_chunk_entities_no_tags_const_impl<Components...>(entityArr, entityCount, chunkIdx, arch, std::forward<Func>(func),
																 std::index_sequence_for<Components...>{});
	}

	template <typename... Components, typename Func>
	void process_chunk_entities_const(const Entity *entityArr, ui entityCount, ui chunkIdx, const Archetype *arch, Func &&func)
	{
		if constexpr (has_tags_v<Components...>)
		{
			process_chunk_entities_with_tags_const<Components...>(entityArr, entityCount, chunkIdx, arch, std::forward<Func>(func));
		}
		else
		{
			process_chunk_entities_no_tags_const<Components...>(entityArr, entityCount, chunkIdx, arch, std::forward<Func>(func));
		}
	}
} // namespace Dimensia::ECS

#endif