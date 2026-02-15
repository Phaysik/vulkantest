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
#include <cstdint>
#include <memory>
#include <vector>

#include "chunkVersion.h"
#include "componentMask.h"
#include "componentRegistry.h"
#include "entity.h"
#include "entityRecord.h"

class Archetype
{
	public:
		explicit Archetype(ComponentMask regularMask);
		~Archetype();

		Archetype(const Archetype &) = delete;
		Archetype &operator=(const Archetype &) = delete;

		std::pair<uint32_t, uint32_t> addEntity(Entity entity, const std::array<const void *, MAX_COMPONENTS> &copyData,
												const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask tags = ComponentMask(0));
		std::pair<Entity, uint32_t> removeEntity(uint32_t chunkIdx, uint32_t slotIdx);
		void compact(std::vector<EntityRecord> &globalRecords);

		// Getters
		ComponentMask getRegularMask() const
		{
			return regularMask_;
		}

		uint32_t getChunkCount() const
		{
			return static_cast<uint32_t>(chunks_.size());
		}

		uint32_t getEntityCount(uint32_t chunkIdx) const;
		void *getComponentArray(uint32_t chunkIdx, ComponentTypeId compId) const;
		Entity *getEntityArray(uint32_t chunkIdx) const;
		bool hasTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeId tagId) const;
		void setTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeId tagId);
		void clearTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeId tagId);
		ComponentMask getTags(uint32_t chunkIdx, uint32_t slotIdx) const;

		const ChunkVersion &getChunkVersion(uint32_t chunkIdx) const
		{
			return chunkVersions_[chunkIdx];
		}

		void bumpChunkVersion(uint32_t chunkIdx)
		{
			chunkVersions_[chunkIdx].bump();
		}

		void bumpComponentVersion(uint32_t chunkIdx, ComponentTypeId compId);

		template <typename F>
		void forEachComponent(F &&func) const
		{
			forEachSetBit(regularMask_, std::forward<F>(func));
		}

	private:
		static constexpr size_t CHUNK_SIZE = 16'384;

		struct Chunk
		{
				alignas(64) std::byte buffer[CHUNK_SIZE];
				uint32_t count = 0;
				uint32_t capacity = 0;
		};

		ComponentMask regularMask_;
		std::vector<std::unique_ptr<Chunk>> chunks_;
		std::vector<uint32_t> freeChunks_;
		uint32_t chunkCapacity_;
		std::array<size_t, MAX_COMPONENTS> componentOffsets_;
		std::array<size_t, MAX_COMPONENTS> componentSizes_;
		size_t entityArrayOffset_ = 0;
		size_t tagBitsetOffset_ = 0;
		std::vector<ComponentTypeId> sortedRegular_;
		std::vector<ChunkVersion> chunkVersions_;

		uint32_t computeCapacity() const;
		void computeLayout(uint32_t capacity);

		uint64_t *getTagBitset(Chunk *chunk) const;
		const uint64_t *getTagBitset(const Chunk *chunk) const;
};

#endif