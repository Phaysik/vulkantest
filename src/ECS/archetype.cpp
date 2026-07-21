/*! \file archetype.cpp
	\brief Contains the function definitions for creating a archetype
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/archetype.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "Core/attributeMacros.h"
#include "ECS/chunkVersion.h"
#include "ECS/componentMask.h"
#include "ECS/componentRegistry.h"
#include "ECS/constants.h"
#include "ECS/entity.h"
#include "ECS/entityRecord.h"
#include "ECS/processChunkHelpers.h"

namespace Dimensia::ECS
{
	using Dimensia::Registry::ComponentInfo;

	// MARK: Constructor and Destructor

	Archetype::Archetype(const ComponentMask &regularMask, const ui archetypeID) : mRegularMask(regularMask), mArchetypeID(archetypeID)
	{
		mComponentOffsets.fill(SIZE_MAX);
		mComponentSizes.fill(0);

		forEachSetBit(mRegularMask, [this](const ComponentTypeID typeID) {
			assert(typeID < ComponentInfos.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const ComponentInfo &info{ComponentInfos[typeID]};

			if (info.size > 0)
			{
				mSortedRegular.push_back(typeID);
			}
		});

		std::ranges::sort(mSortedRegular);

		// NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
		mChunkCapacity = computeCapacity(); // Initialized here because it relies on the forEachSetBit above

		computeLayout(mChunkCapacity);
	}

	Archetype::~Archetype()
	{
		for (auto &chunk : mChunks)
		{
			for (ui slot = 0; slot < chunk->mCount; ++slot)
			{
				for (const ComponentTypeID &compID : mSortedRegular)
				{
					assert(compID < mComponentOffsets.size());
					assert(compID < mComponentSizes.size());

					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					void *ptr = &chunk->mBuffer[mComponentOffsets[compID] + (slot * mComponentSizes[compID])];

					assert(compID < ComponentInfos.size());

					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					ComponentInfos[compID].destructor(ptr);
				}
			}
		}
	}

	// MARK: Getters

	ATTR_NODISCARD ui Archetype::getChunkCount() const noexcept
	{
		return static_cast<ui>(mChunks.size());
	}

	ATTR_NODISCARD const ChunkVersion &Archetype::getChunkVersion(const ui chunkIndex) const
	{
		assert(chunkIndex < mChunkVersions.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		return mChunkVersions[chunkIndex];
	}

	ATTR_NODISCARD ui Archetype::getEntityCount(const ui chunkIndex) const
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		return mChunks[chunkIndex]->mCount;
	}

	ATTR_NODISCARD void *Archetype::getComponentArray(const ui chunkIndex, const ComponentTypeID compID) const
	{
		assert(compID < mComponentOffsets.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		if (mComponentOffsets[compID] == SIZE_MAX)
		{
			return nullptr;
		}

		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		return &mChunks[chunkIndex]->mBuffer[mComponentOffsets[compID]];
	}

	ATTR_NODISCARD Entity *Archetype::getEntityArray(const ui chunkIndex) const
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		assert(mEntityArrayOffset < mChunks[chunkIndex]->mBuffer.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-type-reinterpret-cast)
		return reinterpret_cast<Entity *>(&mChunks[chunkIndex]->mBuffer[mEntityArrayOffset]);
	}

	ATTR_NODISCARD ComponentMask Archetype::getTags(const ui chunkIndex, const ui slotIndex) const
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const ul *tagBits{getTagBitset(mChunks[chunkIndex].get())};

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		return {tagBits[slotIndex * TAG_WORDS_PER_ENTITY], tagBits[(slotIndex * TAG_WORDS_PER_ENTITY) + 1]};
	}

	ATTR_NODISCARD const ul *Archetype::getTagBitset(const ui chunkIndex) const
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		assert(mTagBitsetOffset < mChunks[chunkIndex]->mBuffer.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-type-reinterpret-cast)
		return reinterpret_cast<const ul *>(&mChunks[chunkIndex]->mBuffer[mTagBitsetOffset]);
	}

	// MARK: Setter

	void Archetype::setTag(const ui chunkIndex, const ui slotIndex, const ComponentTypeID tagID)
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		ul *tagBits{getTagBitset(mChunks[chunkIndex].get())};

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		if (tagID < LOWER_HALF_BIT_MASK)
		{
			tagBits[slotIndex * TAG_WORDS_PER_ENTITY] |= (1ULL << tagID);
		}
		else
		{
			tagBits[(slotIndex * TAG_WORDS_PER_ENTITY) + 1] |= (1ULL << (tagID - LOWER_HALF_BIT_MASK));
		}
		// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

		assert(chunkIndex < mChunkVersions.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mChunkVersions[chunkIndex].bump();
	}

	// MARK: Member Functions

	ATTR_NODISCARD bool Archetype::hasTag(const ui chunkIndex, const ui slotIndex, const ComponentTypeID tagID) const
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const ul *tagBits{getTagBitset(mChunks[chunkIndex].get())};

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		if (tagID < LOWER_HALF_BIT_MASK)
		{
			return (tagBits[slotIndex * TAG_WORDS_PER_ENTITY] & (1ULL << tagID)) != 0;
		}

		return (tagBits[(slotIndex * TAG_WORDS_PER_ENTITY) + 1] & (1ULL << (tagID - LOWER_HALF_BIT_MASK))) != 0;
		// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	}

	void Archetype::clearTag(const ui chunkIndex, const ui slotIndex, const ComponentTypeID tagID)
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		ul *tagBits{getTagBitset(mChunks[chunkIndex].get())};

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		if (tagID < LOWER_HALF_BIT_MASK)
		{
			tagBits[slotIndex * TAG_WORDS_PER_ENTITY] &= ~(1ULL << tagID);
		}
		else
		{
			tagBits[(slotIndex * TAG_WORDS_PER_ENTITY) + 1] &= ~(1ULL << (tagID - LOWER_HALF_BIT_MASK));
		}
		// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

		assert(chunkIndex < mChunkVersions.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mChunkVersions[chunkIndex].bump();
	}

	void Archetype::bumpChunkVersion(const ui chunkIndex)
	{
		assert(chunkIndex < mChunkVersions.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mChunkVersions[chunkIndex].bump();
	}

	void Archetype::bumpComponentVersion(const ui chunkIndex, const ComponentTypeID compID)
	{
		assert(chunkIndex < mChunkVersions.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mChunkVersions[chunkIndex].bumpComponent(compID);
	}

	std::pair<ui, ui> Archetype::addEntity(const Entity &entity, const std::array<const void *, MAX_COMPONENTS> &copyData,
										   const std::array<void *, MAX_COMPONENTS> &moveData, const ComponentMask &tags)
	{
		Chunk *chunk{nullptr};
		std::size_t chunkIndex{0};

		acquireFreeChunk(chunk, chunkIndex);

		ui slot{chunk->mCount++};

		assert(mEntityArrayOffset < chunk->mBuffer.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-type-reinterpret-cast)
		Entity *entityArr{reinterpret_cast<Entity *>(&chunk->mBuffer[mEntityArrayOffset])};

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		new (&entityArr[slot]) Entity(entity);

		for (const ComponentTypeID componentTypeID : mSortedRegular)
		{
			assert(componentTypeID < mComponentOffsets.size());
			assert(componentTypeID < mComponentSizes.size());
			assert(componentTypeID < ComponentInfos.size());
			assert(componentTypeID < moveData.size());
			assert(componentTypeID < copyData.size());

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const std::size_t offset{mComponentOffsets[componentTypeID]};
			const std::size_t size{mComponentSizes[componentTypeID]};

			assert(offset + (slot * size) <= chunk->mBuffer.size());

			void *dest = &chunk->mBuffer[offset + (slot * size)];

			const ComponentInfo &info = ComponentInfos[componentTypeID];
			if (moveData[componentTypeID] != nullptr)
			{
				info.moveConstruct(dest, moveData[componentTypeID]);
			}
			else
			{
				assert(copyData[componentTypeID] != nullptr);
				info.copyConstruct(dest, copyData[componentTypeID]);
			}
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		}

		ul *tagBits{getTagBitset(chunk)};

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		tagBits[slot * TAG_WORDS_PER_ENTITY] = tags.mLow;
		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		tagBits[(slot * TAG_WORDS_PER_ENTITY) + 1] = tags.mHigh;

		assert(chunkIndex < mChunkVersions.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mChunkVersions[chunkIndex].bump();

		return {chunkIndex, slot};
	}

	std::pair<Entity, ui> Archetype::removeEntity(const ui chunkIndex, const ui slotIndex)
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		Chunk *chunk{mChunks[chunkIndex].get()};

		assert(slotIndex < chunk->mCount);

		const ui lastSlot{chunk->mCount - 1};

		assert(mEntityArrayOffset < chunk->mBuffer.size());

		for (const ComponentTypeID componentTypeID : mSortedRegular)
		{
			assert(componentTypeID < mComponentOffsets.size());
			assert(componentTypeID < mComponentSizes.size());
			assert(componentTypeID < ComponentInfos.size());

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const std::size_t offset{mComponentOffsets[componentTypeID]};
			const std::size_t size{mComponentSizes[componentTypeID]};

			assert(offset + (slotIndex * size) < chunk->mBuffer.size());

			void *ptr{&chunk->mBuffer[offset + (slotIndex * size)]};

			ComponentInfos[componentTypeID].destructor(ptr);
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		}

		Entity movedEntity{.index = 0, .generation = 0};

		releaseChunk(chunk, movedEntity, slotIndex, lastSlot);

		--chunk->mCount;

		assert(chunkIndex < mChunkVersions.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mChunkVersions[chunkIndex].bump();

		if (chunk->mCount == 0)
		{
			mFreeChunks.push_back(chunkIndex);
		}

		return {movedEntity, slotIndex};
	}

	void Archetype::compact(std::vector<EntityRecord> &globalRecords)
	{
		const std::size_t newSize{mChunkCapacity == 0 ? 0 : (mChunks.size() - mFreeChunks.size())};

		if (mFreeChunks.empty() && mChunks.size() == newSize)
		{
			return;
		}

		ui writeIndex{0};

		for (ui readIndex{0}; readIndex < mChunks.size(); ++readIndex)
		{
			assert(readIndex < mChunks.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			if (mChunks[readIndex]->mCount > 0)
			{
				if (writeIndex != readIndex)
				{
					compactStorage(globalRecords, writeIndex, readIndex);
				}

				++writeIndex;
			}
		}

		mChunks.resize(writeIndex);
		mChunkVersions.resize(writeIndex);
		mFreeChunks.clear();
	}

	// MARK: Private Getters

	ATTR_NODISCARD ul *Archetype::getTagBitset(Chunk *chunk) const
	{
		assert(mTagBitsetOffset < chunk->mBuffer.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-type-reinterpret-cast)
		return reinterpret_cast<ul *>(&chunk->mBuffer[mTagBitsetOffset]);
	}

	ATTR_NODISCARD const ul *Archetype::getTagBitset(const Chunk *chunk) const
	{
		assert(mTagBitsetOffset < chunk->mBuffer.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-type-reinterpret-cast)
		return reinterpret_cast<const ul *>(&chunk->mBuffer[mTagBitsetOffset]);
	}

	// MARK: Private Member Functions

	ATTR_NODISCARD ui Archetype::computeCapacity() const
	{
		std::size_t perEntity{sizeof(Entity)};

		for (const ComponentTypeID componentTypeID : mSortedRegular)
		{
			assert(componentTypeID < ComponentInfos.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			perEntity += ComponentInfos[componentTypeID].size;
		}

		perEntity += sizeof(ul) * TAG_WORDS_PER_ENTITY; // tag bitset (2 words for 128-bit mask)

		// Binary search for the largest capacity that fits in CHUNK_SIZE
		ui low{1};
		ui high{static_cast<ui>(CHUNK_SIZE / perEntity) + 1};

		// Lambda to compute the actual byte layout for a given capacity
		auto layoutFits = [&](const ui testCap) -> bool {
			std::size_t offset{0};
			offset += testCap * sizeof(Entity);
			offset = (offset + alignof(ul) - 1) & ~(alignof(ul) - 1);
			offset += testCap * sizeof(ul) * TAG_WORDS_PER_ENTITY;

			for (const ComponentTypeID componentTypeID : mSortedRegular)
			{
				assert(componentTypeID < ComponentInfos.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const ComponentInfo &info{ComponentInfos[componentTypeID]};

				offset = (offset + info.alignment - 1) & ~(info.alignment - 1);
				offset += testCap * info.size;
			}

			return offset <= CHUNK_SIZE;
		};

		// Find the largest cap where layoutFits returns true
		while (low < high)
		{
			const ui mid{low + ((high - low + 1) / 2)};

			if (layoutFits(mid))
			{
				low = mid;
			}
			else
			{
				high = mid - 1;
			}
		}

		return low;
	}

	void Archetype::computeLayout(ui capacity)
	{
		std::size_t offset{0};
		mEntityArrayOffset = offset;
		offset += capacity * sizeof(Entity);

		offset = (offset + alignof(ul) - 1) & ~(alignof(ul) - 1);
		mTagBitsetOffset = offset;
		offset += capacity * sizeof(ul) * TAG_WORDS_PER_ENTITY;

		for (const ComponentTypeID componentTypeID : mSortedRegular)
		{
			assert(componentTypeID < ComponentInfos.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const ComponentInfo &info = ComponentInfos[componentTypeID];
			offset = (offset + info.alignment - 1) & ~(info.alignment - 1);

			assert(componentTypeID < mComponentOffsets.size());
			assert(componentTypeID < mComponentSizes.size());

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			mComponentOffsets[componentTypeID] = offset;
			mComponentSizes[componentTypeID] = info.size;
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

			offset += capacity * info.size;
		}
		assert(offset <= CHUNK_SIZE);
	}

	void Archetype::acquireFreeChunk(Chunk *&chunk, std::size_t &chunkIndex)
	{
		for (; chunkIndex < mChunks.size(); ++chunkIndex)
		{
			assert(chunkIndex < mChunks.size());

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			if (mChunks[chunkIndex]->mCount < mChunkCapacity)
			{
				chunk = mChunks[chunkIndex].get();
				break;
			}
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		}

		if ((chunk == nullptr) && !mFreeChunks.empty())
		{
			chunkIndex = mFreeChunks.back();
			mFreeChunks.pop_back();

			if (chunkIndex >= mChunks.size())
			{
				chunkIndex = mChunks.size();
				auto newChunk{std::make_unique<Chunk>()};

				newChunk->mCapacity = mChunkCapacity;
				chunk = newChunk.get();

				mChunks.push_back(std::move(newChunk));
				mChunkVersions.emplace_back();
			}
			else
			{
				assert(chunkIndex < mChunks.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				chunk = mChunks[chunkIndex].get();
				chunk->mCount = 0;
			}
		}

		if (chunk == nullptr)
		{
			auto newChunk{std::make_unique<Chunk>()};
			newChunk->mCapacity = mChunkCapacity;

			chunk = newChunk.get();
			mChunks.push_back(std::move(newChunk));
			mChunkVersions.emplace_back();

			chunkIndex = mChunks.size() - 1;
		}
	}

	void Archetype::releaseChunk(Chunk *&chunk, Entity &movedEntity, const ui slotIndex, const ui lastSlot)
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-type-reinterpret-cast)
		Entity *entityArr{reinterpret_cast<Entity *>(&chunk->mBuffer[mEntityArrayOffset])};

		ul *tagBits{getTagBitset(chunk)};

		if (slotIndex != lastSlot)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			movedEntity = entityArr[lastSlot];

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			entityArr[slotIndex] = movedEntity;

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			tagBits[slotIndex * TAG_WORDS_PER_ENTITY] = tagBits[lastSlot * TAG_WORDS_PER_ENTITY];
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			tagBits[(slotIndex * TAG_WORDS_PER_ENTITY) + 1] = tagBits[(lastSlot * TAG_WORDS_PER_ENTITY) + 1];

			moveConstructChunk(chunk, slotIndex, lastSlot);

			destructChunk(chunk, lastSlot);
		}
	}

	void Archetype::moveConstructChunk(Chunk *&chunk, const ui slotIndex, const ui lastSlot)
	{
		for (const ComponentTypeID componentTypeID : mSortedRegular)
		{
			assert(componentTypeID < mComponentOffsets.size());
			assert(componentTypeID < mComponentSizes.size());
			assert(componentTypeID < ComponentInfos.size());

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const std::size_t offset{mComponentOffsets[componentTypeID]};
			const std::size_t size{mComponentSizes[componentTypeID]};

			assert(offset + (slotIndex * size) < chunk->mBuffer.size());
			assert(offset + (lastSlot * size) < chunk->mBuffer.size());

			void *dest{&chunk->mBuffer[offset + (slotIndex * size)]};
			void *src{&chunk->mBuffer[offset + (lastSlot * size)]};

			ComponentInfos[componentTypeID].moveConstruct(dest, src);
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		}
	}

	void Archetype::destructChunk(Chunk *&chunk, const ui lastSlot)
	{
		for (const ComponentTypeID componentTypeID : mSortedRegular)
		{
			assert(componentTypeID < mComponentOffsets.size());
			assert(componentTypeID < mComponentSizes.size());
			assert(componentTypeID < ComponentInfos.size());

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const std::size_t offset{mComponentOffsets[componentTypeID]};
			const std::size_t size{mComponentSizes[componentTypeID]};

			assert(offset + (lastSlot * size) < chunk->mBuffer.size());

			void *ptr{&chunk->mBuffer[offset + (lastSlot * size)]};

			ComponentInfos[componentTypeID].destructor(ptr);
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		}
	}

	void Archetype::compactStorage(std::vector<EntityRecord> &globalRecords, const ui writeIndex, const ui readIndex)
	{
		assert(writeIndex < mChunks.size());
		assert(readIndex < mChunks.size());
		assert(writeIndex < mChunkVersions.size());
		assert(readIndex < mChunkVersions.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		assert(mEntityArrayOffset < mChunks[writeIndex]->mBuffer.size());

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mChunks[writeIndex] = std::move(mChunks[readIndex]);
		mChunkVersions[writeIndex] = mChunkVersions[readIndex];

		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		const Entity *entityArr{reinterpret_cast<Entity *>(&mChunks[writeIndex]->mBuffer[mEntityArrayOffset])};

		for (ui subscript{0}; subscript < mChunks[writeIndex]->mCount; ++subscript)
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			const Entity subscriptEntity{entityArr[subscript]};

			assert(subscriptEntity.index < globalRecords.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			EntityRecord &rec{globalRecords[subscriptEntity.index]};

			// Compare using archetypeId_ instead of pointer
			if (rec.archetypeID == mArchetypeID && rec.chunkIndex == readIndex)
			{
				rec.chunkIndex = writeIndex;
			}
		}
	}
} // namespace Dimensia::ECS