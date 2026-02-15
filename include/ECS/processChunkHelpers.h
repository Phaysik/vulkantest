/*! \file processChunkHelpers.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_PROCESSCHUNKHELPERS_H
#define INCLUDE_ECS_PROCESSCHUNKHELPERS_H

#include <cstdint>
#include <type_traits>

#include "archetype.h"
#include "componentMask.h"
#include "componentRegistry.h"

namespace Dimensia::ECS
{
	using Registry::componentId;
	using Registry::ComponentInfos;
	using Registry::ComponentTypeId;
	using Registry::is_tag_component;

	// Helper to iterate over set bits in a ComponentMask
	template <typename F>
	void forEachSetBit(ComponentMask mask, F &&func)
	{
		uint64_t bits = mask.low;
		while (bits)
		{
			uint64_t t = bits & -bits;
			int idx = __builtin_ctzll(bits);
			func(static_cast<ComponentTypeId>(idx));
			bits ^= t;
		}
		bits = mask.high;
		while (bits)
		{
			uint64_t t = bits & -bits;
			int idx = __builtin_ctzll(bits) + 64;
			func(static_cast<ComponentTypeId>(idx));
			bits ^= t;
		}
	}

	// Build required mask for a set of component types
	template <typename... Components>
	constexpr ComponentMask build_required_mask()
	{
		ComponentMask mask{0, 0};
		(([&] {
			 if constexpr (!is_tag_component<Components>::value)
			 {
				 ComponentTypeId id = componentId<Components>();
				 if (id < 64)
				 {
					 mask.low |= (uint64_t(1) << id);
				 }
				 else
				 {
					 mask.high |= (uint64_t(1) << (id - 64));
				 }
			 }
		 }()),
		 ...);
		return mask;
	}

	// Process a chunk of entities (non‑const version)
	template <typename... Components, typename Func, typename EntityArr, typename CompArrays>
	void process_chunk_entities(EntityArr *entityArr, CompArrays &compArrays, uint32_t entityCount, uint32_t chunkIdx, Archetype *arch,
								Func &&func)
	{
		for (uint32_t s = 0; s < entityCount; ++s)
		{
			Entity e = entityArr[s];
			bool tagsOk = true;
			ComponentMask entityTags = arch->getTags(chunkIdx, s);
			size_t i = 0;
			((tagsOk = tagsOk && (!is_tag_component<Components>::value || (entityTags.low & (uint64_t(1) << componentId<Components>()))),
			  ++i),
			 ...);
			if (!tagsOk)
			{
				continue;
			}

			[&]<size_t... Is>(std::index_sequence<Is...>) {
				func(e, ([&]() -> std::conditional_t<is_tag_component<Components>::value, Components, Components &> {
						 if constexpr (is_tag_component<Components>::value)
						 {
							 return Components{};
						 }
						 else
						 {
							 void *ptr
								 = static_cast<std::byte *>(std::get<Is>(compArrays)) + s * ComponentInfos[componentId<Components>()].size;
							 return static_cast<Components &>(*static_cast<Components *>(ptr));
						 }
					 }())...);
			}(std::index_sequence_for<Components...>{});
		}
	}

	// Process a chunk of entities (const version)
	template <typename... Components, typename Func, typename EntityArr, typename CompArrays>
	void process_chunk_entities_const(const EntityArr *entityArr, const CompArrays &compArrays, uint32_t entityCount, uint32_t chunkIdx,
									  const Archetype *arch, Func &&func)
	{
		for (uint32_t s = 0; s < entityCount; ++s)
		{
			Entity e = entityArr[s];
			bool tagsOk = true;
			ComponentMask entityTags = arch->getTags(chunkIdx, s);
			size_t i = 0;
			((tagsOk = tagsOk && (!is_tag_component<Components>::value || (entityTags.low & (uint64_t(1) << componentId<Components>()))),
			  ++i),
			 ...);
			if (!tagsOk)
			{
				continue;
			}

			[&]<size_t... Is>(std::index_sequence<Is...>) {
				func(e, ([&]() -> std::conditional_t<is_tag_component<Components>::value, Components, const Components &> {
						 if constexpr (is_tag_component<Components>::value)
						 {
							 return Components{};
						 }
						 else
						 {
							 const void *ptr = static_cast<const std::byte *>(std::get<Is>(compArrays))
											 + s * ComponentInfos[componentId<Components>()].size;
							 return static_cast<const Components &>(*static_cast<const Components *>(ptr));
						 }
					 }())...);
			}(std::index_sequence_for<Components...>{});
		}
	}
} // namespace Dimensia::ECS

#endif