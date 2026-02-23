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
#include "ECS/constants.h"
#include "ECS/entity.h"
#include "ECS/entityRecord.h"
#include "ECS/processChunkHelpers.h"

namespace Dimensia::ECS
{
	// MARK: Constructor and Destructor

	Archetype::Archetype(const ComponentMask &regularMask, const ui archetypeID) : mRegularMask(regularMask), mArchetypeID(archetypeID)
	{
		mComponentOffsets.fill(SIZE_MAX);
		mComponentSizes.fill(0);

		forEachSetBit(mRegularMask, [this](const ComponentTypeID typeID) {
			assert(typeID < ComponentInfos.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const auto &info{ComponentInfos[typeID]};
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
			for (ui slot = 0; slot < chunk->count; ++slot)
			{
				for (const ComponentTypeID &compID : mSortedRegular)
				{
					assert(compID < mComponentOffsets.size());
					assert(compID < mComponentSizes.size());

					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					void *ptr = &chunk->buffer[mComponentOffsets[compID] + (slot * mComponentSizes[compID])];

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
		return mChunks[chunkIndex]->count;
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
		return &mChunks[chunkIndex]->buffer[mComponentOffsets[compID]];
	}

	ATTR_NODISCARD Entity *Archetype::getEntityArray(const ui chunkIndex) const
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		assert(mEntityArrayOffset < mChunks[chunkIndex]->buffer.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-type-reinterpret-cast)
		return reinterpret_cast<Entity *>(&mChunks[chunkIndex]->buffer[mEntityArrayOffset]);
	}

	ATTR_NODISCARD ComponentMask Archetype::getTags(const ui chunkIndex, const ui slotIndex) const
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const ul *tagBits{getTagBitset(mChunks[chunkIndex].get())};

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		return {tagBits[slotIndex], 0};
	}

	ATTR_NODISCARD const ul *Archetype::getTagBitset(const ui chunkIndex) const
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		assert(mTagBitsetOffset < mChunks[chunkIndex]->buffer.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-type-reinterpret-cast)
		return reinterpret_cast<const ul *>(&mChunks[chunkIndex]->buffer[mTagBitsetOffset]);
	}

	// MARK: Setter

	void Archetype::setTag(const ui chunkIndex, const ui slotIndex, const ComponentTypeID tagID)
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		ul *tagBits{getTagBitset(mChunks[chunkIndex].get())};

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		tagBits[slotIndex] |= (1U << tagID);

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

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		return (tagBits[slotIndex] & (1U << tagID)) != 0;
	}

	void Archetype::clearTag(const ui chunkIndex, const ui slotIndex, const ComponentTypeID tagID)
	{
		assert(chunkIndex < mChunks.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		ul *tagBits{getTagBitset(mChunks[chunkIndex].get())};

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		tagBits[slotIndex] &= ~(1U << tagID);

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

		for (; chunkIndex < mChunks.size(); ++chunkIndex)
		{
			assert(chunkIndex < mChunks.size());

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			if (mChunks[chunkIndex]->count < mChunkCapacity)
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

				newChunk->capacity = mChunkCapacity;
				chunk = newChunk.get();

				mChunks.push_back(std::move(newChunk));
				mChunkVersions.emplace_back();
			}
			else
			{
				assert(chunkIndex < mChunks.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				chunk = mChunks[chunkIndex].get();
				chunk->count = 0;
			}
		}

		if (chunk == nullptr)
		{
			auto newChunk{std::make_unique<Chunk>()};
			newChunk->capacity = mChunkCapacity;

			chunk = newChunk.get();
			mChunks.push_back(std::move(newChunk));
			mChunkVersions.emplace_back();

			chunkIndex = mChunks.size() - 1;
		}

		ui slot{chunk->count++};

		assert(mEntityArrayOffset < chunk->buffer.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-type-reinterpret-cast)
		Entity *entityArr{reinterpret_cast<Entity *>(&chunk->buffer[mEntityArrayOffset])};

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

			assert(offset + (slot * size) <= chunk->buffer.size());

			void *dest = &chunk->buffer[offset + (slot * size)];

			const auto &info = ComponentInfos[componentTypeID];
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
		tagBits[slot] = tags.mLow;

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

		assert(slotIndex < chunk->count);

		const ui lastSlot{chunk->count - 1};

		assert(mEntityArrayOffset < chunk->buffer.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-type-reinterpret-cast)
		Entity *entityArr{reinterpret_cast<Entity *>(&chunk->buffer[mEntityArrayOffset])};

		ul *tagBits{getTagBitset(chunk)};

		for (const ComponentTypeID componentTypeID : mSortedRegular)
		{
			assert(componentTypeID < mComponentOffsets.size());
			assert(componentTypeID < mComponentSizes.size());
			assert(componentTypeID < ComponentInfos.size());

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const std::size_t offset{mComponentOffsets[componentTypeID]};
			const std::size_t size{mComponentSizes[componentTypeID]};

			assert(offset + (slotIndex * size) < chunk->buffer.size());

			void *ptr{&chunk->buffer[offset + (slotIndex * size)]};

			ComponentInfos[componentTypeID].destructor(ptr);
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		}

		Entity movedEntity{.index = 0, .generation = 0};

		if (slotIndex != lastSlot)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			movedEntity = entityArr[lastSlot];

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			entityArr[slotIndex] = movedEntity;

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
			tagBits[slotIndex] = tagBits[lastSlot];

			for (const ComponentTypeID componentTypeID : mSortedRegular)
			{
				assert(componentTypeID < mComponentOffsets.size());
				assert(componentTypeID < mComponentSizes.size());
				assert(componentTypeID < ComponentInfos.size());

				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const std::size_t offset{mComponentOffsets[componentTypeID]};
				const std::size_t size{mComponentSizes[componentTypeID]};

				assert(offset + (slotIndex * size) < chunk->buffer.size());
				assert(offset + (lastSlot * size) < chunk->buffer.size());

				void *dest{&chunk->buffer[offset + (slotIndex * size)]};
				void *src{&chunk->buffer[offset + (lastSlot * size)]};

				ComponentInfos[componentTypeID].moveConstruct(dest, src);
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			}

			for (const ComponentTypeID componentTypeID : mSortedRegular)
			{
				assert(componentTypeID < mComponentOffsets.size());
				assert(componentTypeID < mComponentSizes.size());
				assert(componentTypeID < ComponentInfos.size());

				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const std::size_t offset{mComponentOffsets[componentTypeID]};
				const std::size_t size{mComponentSizes[componentTypeID]};

				assert(offset + (lastSlot * size) < chunk->buffer.size());

				void *ptr{&chunk->buffer[offset + (lastSlot * size)]};

				ComponentInfos[componentTypeID].destructor(ptr);
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			}
		}

		--chunk->count;

		assert(chunkIndex < mChunkVersions.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mChunkVersions[chunkIndex].bump();

		if (chunk->count == 0)
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
			if (mChunks[readIndex]->count > 0)
			{
				if (writeIndex != readIndex)
				{
					assert(writeIndex < mChunks.size());
					assert(readIndex < mChunks.size());
					assert(writeIndex < mChunkVersions.size());
					assert(readIndex < mChunkVersions.size());

					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					assert(mEntityArrayOffset < mChunks[writeIndex]->buffer.size());

					// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					mChunks[writeIndex] = std::move(mChunks[readIndex]);
					mChunkVersions[writeIndex] = mChunkVersions[readIndex];

					// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
					const Entity *entityArr{reinterpret_cast<Entity *>(&mChunks[writeIndex]->buffer[mEntityArrayOffset])};

					for (ui subscript{0}; subscript < mChunks[writeIndex]->count; ++subscript)
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
		assert(mTagBitsetOffset < chunk->buffer.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-type-reinterpret-cast)
		return reinterpret_cast<ul *>(&chunk->buffer[mTagBitsetOffset]);
	}

	ATTR_NODISCARD const ul *Archetype::getTagBitset(const Chunk *chunk) const
	{
		assert(mTagBitsetOffset < chunk->buffer.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-type-reinterpret-cast)
		return reinterpret_cast<const ul *>(&chunk->buffer[mTagBitsetOffset]);
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

		perEntity += sizeof(ul); // tag bitset
		ui cap{static_cast<ui>(CHUNK_SIZE / perEntity) + 1};

		while (true)
		{
			std::size_t offset{0};
			offset += cap * sizeof(Entity);
			offset = (offset + alignof(ul) - 1) & ~(alignof(ul) - 1);
			offset += cap * sizeof(ul);

			for (const ComponentTypeID componentTypeID : mSortedRegular)
			{
				assert(componentTypeID < ComponentInfos.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const auto &info{ComponentInfos[componentTypeID]};

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
		std::size_t offset{0};
		mEntityArrayOffset = offset;
		offset += capacity * sizeof(Entity);

		offset = (offset + alignof(ul) - 1) & ~(alignof(ul) - 1);
		mTagBitsetOffset = offset;
		offset += capacity * sizeof(ul);

		for (const ComponentTypeID componentTypeID : mSortedRegular)
		{
			assert(componentTypeID < ComponentInfos.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const auto &info = ComponentInfos[componentTypeID];
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
} // namespace Dimensia::ECS