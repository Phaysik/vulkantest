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

#include "ECS/processChunkHelpers.h"

Archetype::Archetype(ComponentMask regularMask) : regularMask_(regularMask), chunks_(), freeChunks_(), chunkCapacity_(0), chunkVersions_()
{
	componentOffsets_.fill(SIZE_MAX);
	componentSizes_.fill(0);

	forEachSetBit(regularMask_, [this](ComponentTypeId id) {
		const auto &info = ComponentInfos[id];
		if (info.size > 0)
		{
			sortedRegular_.push_back(id);
		}
	});

	std::sort(sortedRegular_.begin(), sortedRegular_.end());
	chunkCapacity_ = computeCapacity();
	computeLayout(chunkCapacity_);
}

Archetype::~Archetype()
{
	for (auto &chunk : chunks_)
	{
		for (uint32_t slot = 0; slot < chunk->count; ++slot)
		{
			for (ComponentTypeId compId : sortedRegular_)
			{
				void *ptr = static_cast<std::byte *>(chunk->buffer) + componentOffsets_[compId] + slot * componentSizes_[compId];
				ComponentInfos[compId].destructor(ptr);
			}
		}
	}
}

uint32_t Archetype::computeCapacity() const
{
	size_t perEntity = sizeof(Entity);
	for (ComponentTypeId id : sortedRegular_)
	{
		perEntity += ComponentInfos[id].size;
	}
	perEntity += sizeof(uint64_t); // tag bitset

	uint32_t cap = static_cast<uint32_t>(CHUNK_SIZE / perEntity) + 1;
	while (true)
	{
		size_t offset = 0;
		offset += cap * sizeof(Entity);
		offset = (offset + alignof(uint64_t) - 1) & ~(alignof(uint64_t) - 1);
		offset += cap * sizeof(uint64_t);

		for (ComponentTypeId id : sortedRegular_)
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

void Archetype::computeLayout(uint32_t capacity)
{
	size_t offset = 0;
	entityArrayOffset_ = offset;
	offset += capacity * sizeof(Entity);

	offset = (offset + alignof(uint64_t) - 1) & ~(alignof(uint64_t) - 1);
	tagBitsetOffset_ = offset;
	offset += capacity * sizeof(uint64_t);

	for (ComponentTypeId id : sortedRegular_)
	{
		const auto &info = ComponentInfos[id];
		offset = (offset + info.alignment - 1) & ~(info.alignment - 1);
		componentOffsets_[id] = offset;
		componentSizes_[id] = info.size;
		offset += capacity * info.size;
	}
	assert(offset <= CHUNK_SIZE);
}

std::pair<uint32_t, uint32_t> Archetype::addEntity(Entity entity, const std::array<const void *, MAX_COMPONENTS> &copyData,
												   const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask tags)
{
	Chunk *chunk = nullptr;
	uint32_t chunkIdx = 0;

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
		chunkIdx = static_cast<uint32_t>(chunks_.size() - 1);
	}

	uint32_t slot = chunk->count++;
	Entity *entityArr = reinterpret_cast<Entity *>(chunk->buffer + entityArrayOffset_);
	new (&entityArr[slot]) Entity(entity);

	for (ComponentTypeId id : sortedRegular_)
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

	uint64_t *tagBits = getTagBitset(chunk);
	tagBits[slot] = tags.low;

	chunkVersions_[chunkIdx].bump();
	return {chunkIdx, slot};
}

std::pair<Entity, uint32_t> Archetype::removeEntity(uint32_t chunkIdx, uint32_t slotIdx)
{
	Chunk *chunk = chunks_[chunkIdx].get();
	assert(slotIdx < chunk->count);

	uint32_t lastSlot = chunk->count - 1;
	Entity *entityArr = reinterpret_cast<Entity *>(chunk->buffer + entityArrayOffset_);
	uint64_t *tagBits = getTagBitset(chunk);

	for (ComponentTypeId id : sortedRegular_)
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

		for (ComponentTypeId id : sortedRegular_)
		{
			size_t offset = componentOffsets_[id];
			size_t size = componentSizes_[id];
			void *dest = chunk->buffer + offset + slotIdx * size;
			void *src = chunk->buffer + offset + lastSlot * size;
			ComponentInfos[id].moveConstruct(dest, src);
		}
		for (ComponentTypeId id : sortedRegular_)
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

void *Archetype::getComponentArray(uint32_t chunkIdx, ComponentTypeId compId) const
{
	if (componentOffsets_[compId] == SIZE_MAX)
	{
		return nullptr;
	}
	return chunks_[chunkIdx]->buffer + componentOffsets_[compId];
}

Entity *Archetype::getEntityArray(uint32_t chunkIdx) const
{
	return reinterpret_cast<Entity *>(chunks_[chunkIdx]->buffer + entityArrayOffset_);
}

bool Archetype::hasTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeId tagId) const
{
	const uint64_t *tagBits = getTagBitset(chunks_[chunkIdx].get());
	return (tagBits[slotIdx] & (uint64_t(1) << tagId)) != 0;
}

void Archetype::setTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeId tagId)
{
	uint64_t *tagBits = getTagBitset(chunks_[chunkIdx].get());
	tagBits[slotIdx] |= (uint64_t(1) << tagId);
	chunkVersions_[chunkIdx].bump();
}

void Archetype::clearTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeId tagId)
{
	uint64_t *tagBits = getTagBitset(chunks_[chunkIdx].get());
	tagBits[slotIdx] &= ~(uint64_t(1) << tagId);
	chunkVersions_[chunkIdx].bump();
}

ComponentMask Archetype::getTags(uint32_t chunkIdx, uint32_t slotIdx) const
{
	const uint64_t *tagBits = getTagBitset(chunks_[chunkIdx].get());
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
					if (rec.archetype == this && rec.chunkIndex == readIdx)
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

uint64_t *Archetype::getTagBitset(Chunk *chunk) const
{
	return reinterpret_cast<uint64_t *>(chunk->buffer + tagBitsetOffset_);
}

const uint64_t *Archetype::getTagBitset(const Chunk *chunk) const
{
	return reinterpret_cast<const uint64_t *>(chunk->buffer + tagBitsetOffset_);
}

uint32_t Archetype::getEntityCount(uint32_t chunkIdx) const
{
	return chunks_[chunkIdx]->count;
}

void Archetype::bumpComponentVersion(uint32_t chunkIdx, ComponentTypeId compId)
{
	chunkVersions_[chunkIdx].bumpComponent(compId);
}