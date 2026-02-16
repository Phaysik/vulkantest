/*! \file archetype.cpp
	\brief Contains the function definitions for creating a archetype
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/archetype.h"

#include <algorithm>
#include <cassert>

#include "Core/attributeMacros.h"
#include "ECS/entityRecord.h"
#include "ECS/processChunkHelpers.h"

namespace Dimensia::ECS
{
	Archetype::Archetype(ComponentMask regularMask, uint32_t id)
		: regularMask_(regularMask), archetypeId_(id), chunks_(), freeChunks_(), chunkCapacity_(0), chunkVersions_()
	{
		componentOffsets_.fill(SIZE_MAX);
		componentSizes_.fill(0);

		forEachSetBit(regularMask_, [this](ComponentTypeID typeID) {
			const auto &info = ComponentInfos.at(typeID);
			if (info.size > 0)
			{
				sortedRegular_.push_back(typeID);
			}
		});

		std::ranges::sort(sortedRegular_);
		chunkCapacity_ = computeCapacity();
		computeLayout(chunkCapacity_);
	}

	Archetype::~Archetype()
	{
		for (auto &chunk : chunks_)
		{
			for (ui slot = 0; slot < chunk->count; ++slot)
			{
				for (const ComponentTypeID &compId : sortedRegular_)
				{
					void *ptr
						= static_cast<std::byte *>(chunk->buffer) + componentOffsets_.at(compId) + (slot * componentSizes_.at(compId));
					ComponentInfos.at(compId).destructor(ptr);
				}
			}
		}
	}

	ATTR_NODISCARD ComponentMask Archetype::getRegularMask() const
	{
		return regularMask_;
	}

	ATTR_NODISCARD ui Archetype::getChunkCount() const
	{
		return static_cast<ui>(chunks_.size());
	}

	ATTR_NODISCARD ui Archetype::getId() const
	{
		return archetypeId_;
	}

	ATTR_NODISCARD const ChunkVersion &Archetype::getChunkVersion(ui chunkIdx) const
	{
		return chunkVersions_.at(chunkIdx);
	}

	ATTR_NODISCARD ui Archetype::computeCapacity() const
	{
		size_t perEntity = sizeof(Entity);
		for (ComponentTypeID id : sortedRegular_)
		{
			perEntity += ComponentInfos[id].size;
		}
		perEntity += sizeof(ul); // tag bitset

		ui cap = static_cast<ui>(CHUNK_SIZE / perEntity) + 1;
		while (true)
		{
			size_t offset = 0;
			offset += cap * sizeof(Entity);
			offset = (offset + alignof(ul) - 1) & ~(alignof(ul) - 1);
			offset += cap * sizeof(ul);

			for (ComponentTypeID id : sortedRegular_)
			{
				const auto &info = ComponentInfos[id];
				offset = (offset + info.alignment - 1) & ~(info.alignment - 1);
				offset += cap * info.size;
			}
			if (offset <= CHUNK_SIZE)
			{
				break;
			}
			--cap;
			assert(cap > 0);
		}
		return cap;
	}

	void Archetype::computeLayout(ui capacity)
	{
		size_t offset = 0;
		entityArrayOffset_ = offset;
		offset += capacity * sizeof(Entity);

		offset = (offset + alignof(ul) - 1) & ~(alignof(ul) - 1);
		tagBitsetOffset_ = offset;
		offset += capacity * sizeof(ul);

		for (ComponentTypeID id : sortedRegular_)
		{
			const auto &info = ComponentInfos[id];
			offset = (offset + info.alignment - 1) & ~(info.alignment - 1);
			componentOffsets_[id] = offset;
			componentSizes_[id] = info.size;
			offset += capacity * info.size;
		}
		assert(offset <= CHUNK_SIZE);
	}

	std::pair<ui, ui> Archetype::addEntity(Entity entity, const std::array<const void *, MAX_COMPONENTS> &copyData,
										   const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask tags)
	{
		Chunk *chunk = nullptr;
		ui chunkIdx = 0;

		for (; chunkIdx < chunks_.size(); ++chunkIdx)
		{
			if (chunks_[chunkIdx]->count < chunkCapacity_)
			{
				chunk = chunks_[chunkIdx].get();
				break;
			}
		}

		if (!chunk && !freeChunks_.empty())
		{
			chunkIdx = freeChunks_.back();
			freeChunks_.pop_back();
			if (chunkIdx >= chunks_.size())
			{
				chunkIdx = chunks_.size();
				auto newChunk = std::make_unique<Chunk>();
				newChunk->capacity = chunkCapacity_;
				chunk = newChunk.get();
				chunks_.push_back(std::move(newChunk));
				chunkVersions_.emplace_back();
			}
			else
			{
				chunk = chunks_[chunkIdx].get();
				chunk->count = 0;
			}
		}

		if (!chunk)
		{
			auto newChunk = std::make_unique<Chunk>();
			newChunk->capacity = chunkCapacity_;
			chunk = newChunk.get();
			chunks_.push_back(std::move(newChunk));
			chunkVersions_.emplace_back();
			chunkIdx = static_cast<ui>(chunks_.size() - 1);
		}

		ui slot = chunk->count++;
		Entity *entityArr = reinterpret_cast<Entity *>(chunk->buffer + entityArrayOffset_);
		new (&entityArr[slot]) Entity(entity);

		for (ComponentTypeID id : sortedRegular_)
		{
			size_t offset = componentOffsets_[id];
			size_t size = componentSizes_[id];
			void *dest = chunk->buffer + offset + slot * size;

			const auto &info = ComponentInfos[id];
			if (moveData[id] != nullptr)
			{
				info.moveConstruct(dest, moveData[id]);
			}
			else
			{
				assert(copyData[id] != nullptr);
				info.copyConstruct(dest, copyData[id]);
			}
		}

		ul *tagBits = getTagBitset(chunk);
		tagBits[slot] = tags.low;

		chunkVersions_[chunkIdx].bump();
		return {chunkIdx, slot};
	}

	std::pair<Entity, ui> Archetype::removeEntity(ui chunkIdx, ui slotIdx)
	{
		Chunk *chunk = chunks_[chunkIdx].get();
		assert(slotIdx < chunk->count);

		ui lastSlot = chunk->count - 1;
		Entity *entityArr = reinterpret_cast<Entity *>(chunk->buffer + entityArrayOffset_);
		ul *tagBits = getTagBitset(chunk);

		for (ComponentTypeID id : sortedRegular_)
		{
			size_t offset = componentOffsets_[id];
			size_t size = componentSizes_[id];
			void *ptr = chunk->buffer + offset + slotIdx * size;
			ComponentInfos[id].destructor(ptr);
		}

		Entity movedEntity{0, 0};
		if (slotIdx != lastSlot)
		{
			movedEntity = entityArr[lastSlot];
			entityArr[slotIdx] = movedEntity;
			tagBits[slotIdx] = tagBits[lastSlot];

			for (ComponentTypeID id : sortedRegular_)
			{
				size_t offset = componentOffsets_[id];
				size_t size = componentSizes_[id];
				void *dest = chunk->buffer + offset + slotIdx * size;
				void *src = chunk->buffer + offset + lastSlot * size;
				ComponentInfos[id].moveConstruct(dest, src);
			}
			for (ComponentTypeID id : sortedRegular_)
			{
				size_t offset = componentOffsets_[id];
				size_t size = componentSizes_[id];
				void *ptr = chunk->buffer + offset + lastSlot * size;
				ComponentInfos[id].destructor(ptr);
			}
		}

		--chunk->count;
		chunkVersions_[chunkIdx].bump();

		if (chunk->count == 0)
		{
			freeChunks_.push_back(chunkIdx);
		}

		return {movedEntity, slotIdx};
	}

	ATTR_NODISCARD void *Archetype::getComponentArray(ui chunkIdx, ComponentTypeID compId) const
	{
		if (componentOffsets_[compId] == SIZE_MAX)
		{
			return nullptr;
		}
		return chunks_[chunkIdx]->buffer + componentOffsets_[compId];
	}

	ATTR_NODISCARD Entity *Archetype::getEntityArray(ui chunkIdx) const
	{
		return reinterpret_cast<Entity *>(chunks_[chunkIdx]->buffer + entityArrayOffset_);
	}

	ATTR_NODISCARD bool Archetype::hasTag(ui chunkIdx, ui slotIdx, ComponentTypeID tagId) const
	{
		const ul *tagBits = getTagBitset(chunks_[chunkIdx].get());
		return (tagBits[slotIdx] & (ul(1) << tagId)) != 0;
	}

	void Archetype::setTag(ui chunkIdx, ui slotIdx, ComponentTypeID tagId)
	{
		ul *tagBits = getTagBitset(chunks_[chunkIdx].get());
		tagBits[slotIdx] |= (ul(1) << tagId);
		chunkVersions_[chunkIdx].bump();
	}

	void Archetype::clearTag(ui chunkIdx, ui slotIdx, ComponentTypeID tagId)
	{
		ul *tagBits = getTagBitset(chunks_[chunkIdx].get());
		tagBits[slotIdx] &= ~(ul(1) << tagId);
		chunkVersions_[chunkIdx].bump();
	}

	ATTR_NODISCARD ComponentMask Archetype::getTags(ui chunkIdx, ui slotIdx) const
	{
		const ul *tagBits = getTagBitset(chunks_[chunkIdx].get());
		return ComponentMask(tagBits[slotIdx], 0);
	}

	void Archetype::compact(std::vector<EntityRecord> &globalRecords)
	{
		size_t newSize = chunkCapacity_ == 0 ? 0 : (chunks_.size() - freeChunks_.size());
		if (freeChunks_.empty() && chunks_.size() == newSize)
		{
			return;
		}

		uint32_t writeIdx = 0;
		for (uint32_t readIdx = 0; readIdx < chunks_.size(); ++readIdx)
		{
			if (chunks_[readIdx]->count > 0)
			{
				if (writeIdx != readIdx)
				{
					chunks_[writeIdx] = std::move(chunks_[readIdx]);
					chunkVersions_[writeIdx] = chunkVersions_[readIdx];
					Entity *entityArr = reinterpret_cast<Entity *>(chunks_[writeIdx]->buffer + entityArrayOffset_);
					for (uint32_t s = 0; s < chunks_[writeIdx]->count; ++s)
					{
						Entity e = entityArr[s];
						auto &rec = globalRecords[e.index];
						// Compare using archetypeId_ instead of pointer
						if (rec.archetypeId == archetypeId_ && rec.chunkIndex == readIdx)
						{
							rec.chunkIndex = writeIdx;
						}
					}
				}
				++writeIdx;
			}
		}
		chunks_.resize(writeIdx);
		chunkVersions_.resize(writeIdx);
		freeChunks_.clear();
	}

	ul *Archetype::getTagBitset(Chunk *chunk) const
	{
		return reinterpret_cast<ul *>(chunk->buffer + tagBitsetOffset_);
	}

	const ul *Archetype::getTagBitset(const Chunk *chunk) const
	{
		return reinterpret_cast<const ul *>(chunk->buffer + tagBitsetOffset_);
	}

	ATTR_NODISCARD ui Archetype::getEntityCount(ui chunkIdx) const
	{
		return chunks_[chunkIdx]->count;
	}

	void Archetype::bumpComponentVersion(ui chunkIdx, ComponentTypeID compId)
	{
		chunkVersions_[chunkIdx].bumpComponent(compId);
	}

	void Archetype::bumpChunkVersion(ui chunkIdx)
	{
		chunkVersions_.at(chunkIdx).bump();
	}
} // namespace Dimensia::ECS