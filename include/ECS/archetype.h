/*! \file archetype.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_ARCHETYPE_H
#define INCLUDE_ECS_ARCHETYPE_H

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "Core/attributeMacros.h"
#include "Core/typedefs.h"

#include "chunkVersion.h"
#include "componentMask.h"
#include "componentRegistry.h"
#include "entity.h"
#include "entityRecord.h"

namespace Dimensia::ECS
{
	using Dimensia::Core::ui;
	using Dimensia::Core::ul;

	using Dimensia::Registry::ComponentInfos;
	using Dimensia::Registry::ComponentTypeID;
	using Dimensia::Registry::MAX_COMPONENTS;

	class Archetype
	{

		public:
			// MARK: Constructor, Destructor, and Assignment Operators

			explicit Archetype(const ComponentMask &regularMask, const ui archetypeID);

			Archetype(const Archetype &) = delete; // Cannot exist as std::unique_ptr is only movable
			Archetype(Archetype &&) noexcept = default;
			Archetype &operator=(const Archetype &) = delete; // Cannot exist as std::unique_ptr is only movable
			Archetype &operator=(Archetype &&) noexcept = default;

			~Archetype();

			// MARK: Getters

			ATTR_NODISCARD constexpr ComponentMask getRegularMask() const noexcept
			{
				return mRegularMask;
			}

			ATTR_NODISCARD constexpr ui getId() const noexcept
			{
				return mArchetypeID;
			}

			ATTR_NODISCARD ui getChunkCount() const noexcept;

			ATTR_NODISCARD const ChunkVersion &getChunkVersion(const ui chunkIndex) const;

			ATTR_NODISCARD ui getEntityCount(const ui chunkIndex) const;

			ATTR_NODISCARD void *getComponentArray(const ui chunkIndex, const ComponentTypeID compID) const;

			ATTR_NODISCARD Entity *getEntityArray(const ui chunkIndex) const;

			ATTR_NODISCARD ComponentMask getTags(const ui chunkIndex, const ui slotIndex) const;

			ATTR_NODISCARD const ul *getTagBitset(const ui chunkIndex) const;

			// MARK: Setter

			void setTag(const ui chunkIndex, const ui slotIndex, const ComponentTypeID tagID);

			// MARK: Member Functions

			ATTR_NODISCARD bool hasTag(const ui chunkIndex, const ui slotIndex, const ComponentTypeID tagID) const;

			void clearTag(const ui chunkIndex, const ui slotIndex, const ComponentTypeID tagID);

			void bumpChunkVersion(const ui chunkIndex);

			void bumpComponentVersion(const ui chunkIndex, const ComponentTypeID compID);

			std::pair<ui, ui> addEntity(const Entity &entity, const std::array<const void *, MAX_COMPONENTS> &copyData,
										const std::array<void *, MAX_COMPONENTS> &moveData, const ComponentMask &tags = ComponentMask(0));
			std::pair<Entity, ui> removeEntity(const ui chunkIndex, const ui slotIndex);

			void compact(std::vector<EntityRecord> &globalRecords);

			// MARK: Template Member Function

			template <typename F>
			constexpr void forEachComponent(F &&func) const noexcept
			{
				forEachSetBit(mRegularMask, std::forward<F>(func));
			}

		private:
			struct Chunk
			{
					alignas(CHUNK_ALIGNMENT) std::array<std::byte, CHUNK_SIZE> buffer{};
					ui count = 0;
					ui capacity = 0;
			};

			// MARK: Private Getters

			ATTR_NODISCARD ul *getTagBitset(Chunk *chunk) const;

			ATTR_NODISCARD const ul *getTagBitset(const Chunk *chunk) const;

			// MARK: Private Member Functions

			ATTR_NODISCARD ui computeCapacity() const;

			void computeLayout(ui capacity);

		private:
			ComponentMask mRegularMask;

			std::vector<std::unique_ptr<Chunk>> mChunks;
			std::vector<ui> mFreeChunks;
			std::vector<ComponentTypeID> mSortedRegular;
			std::vector<ChunkVersion> mChunkVersions;

			std::array<std::size_t, MAX_COMPONENTS> mComponentOffsets{};
			std::array<std::size_t, MAX_COMPONENTS> mComponentSizes{};

			std::size_t mEntityArrayOffset{0};
			std::size_t mTagBitsetOffset{0};

			ui mArchetypeID; // stable ID assigned by ECS
			ui mChunkCapacity;
	};
} // namespace Dimensia::ECS

#endif