/*! \file processChunkHelpers.h
	\brief Contains the function declarations for creating a Detailed file description
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

	// Build required mask for a set of component types
	template <typename... Components>
	constexpr ComponentMask build_required_mask()
	{
		ComponentMask mask{0, 0};
		(([&] {
			 if constexpr (!is_tag_component<Components>::value)
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

	// Process a chunk of entities (non‑const version)
	template <typename... Components, typename Func, typename EntityArr, typename CompArrays>
	void process_chunk_entities(const EntityArr *entityArr, const CompArrays &compArrays, ui entityCount, ui chunkIdx,
								const Archetype *arch, Func &&func)
	{
		for (ui slotIndex{0}; slotIndex < entityCount; ++slotIndex)
		{
			Entity entity{entityArr[slotIndex]};
			bool tagsOk{true};
			const ComponentMask entityTags{arch->getTags(chunkIdx, slotIndex)};

			// Check if entity has all required tag components
			(
				[&] {
					if constexpr (is_tag_component<Components>::value)
					{
						const ComponentTypeID compId = componentId<Components>();
						if (!(entityTags.mLow & (1U << compId)))
						{
							tagsOk = false;
						}
					}
				}(),
				...);

			if (!tagsOk)
			{
				continue;
			}

			[&]<std::size_t... Is>(std::index_sequence<Is...>) {
				std::forward<Func>(func)(entity,
										 ([&]() -> std::conditional_t<is_tag_component<Components>::value, Components, Components &> {
											 if constexpr (is_tag_component<Components>::value)
											 {
												 return Components{};
											 }
											 else
											 {
												 void *ptr = static_cast<std::byte *>(std::get<Is>(compArrays))
														   + (slotIndex * ComponentInfos[componentId<Components>()].size);
												 return (*static_cast<Components *>(ptr));
											 }
										 }())...);
			}(std::index_sequence_for<Components...>{});
		}
	}

	// Process a chunk of entities (const version)
	template <typename... Components, typename Func, typename EntityArr, typename CompArrays>
	void process_chunk_entities_const(const EntityArr *entityArr, const CompArrays &compArrays, ui entityCount, ui chunkIdx,
									  const Archetype *arch, Func &&func)
	{
		for (ui slotIndex{0}; slotIndex < entityCount; ++slotIndex)
		{
			Entity entity{entityArr[slotIndex]};
			bool tagsOk{true};
			const ComponentMask entityTags{arch->getTags(chunkIdx, slotIndex)};

			// Check if entity has all required tag components
			(
				[&] {
					if constexpr (is_tag_component<Components>::value)
					{
						const ComponentTypeID compId = componentId<Components>();
						if (!(entityTags.mLow & (1U << compId)))
						{
							tagsOk = false;
						}
					}
				}(),
				...);

			if (!tagsOk)
			{
				continue;
			}

			[&]<std::size_t... Is>(std::index_sequence<Is...>) {
				std::forward<Func>(func)(entity,
										 ([&]() -> std::conditional_t<is_tag_component<Components>::value, Components, const Components &> {
											 if constexpr (is_tag_component<Components>::value)
											 {
												 return Components{};
											 }
											 else
											 {
												 const void *ptr = static_cast<const std::byte *>(std::get<Is>(compArrays))
																 + (slotIndex * ComponentInfos[componentId<Components>()].size);
												 return (*static_cast<const Components *>(ptr));
											 }
										 }())...);
			}(std::index_sequence_for<Components...>{});
		}
	}
} // namespace Dimensia::ECS

#endif