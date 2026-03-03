/*! \file ecs.cpp
	\brief Contains the function definitions for creating a ecs
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/ecs.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "ECS/archetype.h"
#include "ECS/componentMask.h"
#include "ECS/componentRegistry.h"
#include "ECS/constants.h"
#include "ECS/entity.h"
#include "ECS/entityRecord.h"
#include "ECS/processChunkHelpers.h"

namespace Dimensia::ECS
{
	// MARK: Constructor

	ECS::ECS() : mRecords(0), mFreeIndices(0), mArchetypePtrs(0), mParent(0), mChildren(0)
	{
		getOrCreateArchetype(ComponentMask(0));
	}

	// MARK: Getters

	std::vector<Entity> ECS::getChildren(const Entity &parent) const
	{
		const std::scoped_lock<std::mutex> lock(mHierarchyMutex);

		if (parent.index < mChildren.size())
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			return mChildren[parent.index];
		}

		return {};
	}

	Entity ECS::getParent(const Entity &child) const
	{
		const std::scoped_lock<std::mutex> lock(mHierarchyMutex);

		if (child.index < mParent.size())
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			return mParent[child.index];
		}

		return NULL_ENTITY;
	}

	// MARK: Setter

	void ECS::setParent(const Entity &child, const Entity &parent)
	{
		if (parent == child)
		{
			return;
		}

		if (!alive(child))
		{
			return;
		}

		if (parent != NULL_ENTITY && !alive(parent))
		{
			return;
		}

		const std::scoped_lock<std::mutex> lock(mHierarchyMutex);
		ui maxIdx{std::max(child.index, parent.index)};

		if (mParent.size() <= maxIdx)
		{
			mParent.resize(maxIdx + 1, NULL_ENTITY);
			mChildren.resize(maxIdx + 1);
		}

		assert(child.index < mParent.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const Entity oldParent{(child.index < mParent.size()) ? mParent[child.index] : NULL_ENTITY};

		if (oldParent == parent)
		{
			return;
		}

		if (oldParent != NULL_ENTITY && oldParent.index < mChildren.size())
		{
			assert(oldParent.index < mChildren.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			std::vector<Entity> &siblings{mChildren[oldParent.index]};

			std::erase(siblings, child);
		}

		assert(child.index < mParent.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mParent[child.index] = parent;

		if (parent != NULL_ENTITY)
		{
			if (parent.index >= mChildren.size())
			{
				mChildren.resize(parent.index + 1);
			}

			assert(parent.index < mChildren.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			mChildren[parent.index].push_back(child);
		}
	}

	// MARK: Member Functions

	Entity ECS::createEntity()
	{
		ui index{};
		ui gen{};

		if (!mFreeIndices.empty())
		{
			index = mFreeIndices.back();
			mFreeIndices.pop_back();

			assert(index < mRecords.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			gen = mRecords[index].generation;
		}
		else
		{
			index = mNextEntityIndex++;
			mRecords.resize(index + 1);
			gen = 1;

			assert(index < mRecords.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			mRecords[index].generation = gen;
		}

		Entity entity{.index = index, .generation = gen, .state = State::Initializing};

		Archetype *emptyArch{getOrCreateArchetype(ComponentMask(0))};
		std::array<const void *, MAX_COMPONENTS> noCopy{};
		std::array<void *, MAX_COMPONENTS> noMove{};

		noCopy.fill(nullptr);
		noMove.fill(nullptr);

		auto [chunk, slot]{emptyArch->addEntity(entity, noCopy, noMove, ComponentMask(0))};

		assert(index < mRecords.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mRecords[index] = {.generation = gen, .archetypeID = emptyArch->getId(), .chunkIndex = chunk, .slotIndex = slot};

		const std::scoped_lock<std::mutex> lock(mHierarchyMutex);

		if (mParent.size() <= index)
		{
			mParent.resize(index + 1, NULL_ENTITY);
			mChildren.resize(index + 1);
		}

		entity.state = State::Active;

		return entity;
	}

	void ECS::destroyEntity(Entity &entity, const bool destroyChildren)
	{
		if (!alive(entity))
		{
			return;
		}

		entity.state = State::Destroying;

		if (destroyChildren)
		{
			destroyHierarchy(entity);
		}

		{
			const std::scoped_lock<std::mutex> lock(mHierarchyMutex);

			assert(entity.index < mParent.size());

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			if (entity.index < mParent.size() && mParent[entity.index] != NULL_ENTITY)
			{
				const Entity parent{mParent[entity.index]};

				if (parent.index < mChildren.size())
				{
					assert(parent.index < mChildren.size());

					std::vector<Entity> &siblings{mChildren[parent.index]};
					std::erase(siblings, entity);
				}

				mParent[entity.index] = NULL_ENTITY;
			}

			assert(entity.index < mChildren.size());

			if (entity.index < mChildren.size())
			{
				mChildren[entity.index].clear();
			}
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		}

		assert(entity.index < mRecords.size());

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		EntityRecord &rec{mRecords[entity.index]};

		assert(rec.archetypeID < mArchetypePtrs.size());

		Archetype *arch{mArchetypePtrs[rec.archetypeID].get()};

		auto [movedEntity, newSlot]{arch->removeEntity(rec.chunkIndex, rec.slotIndex)};

		if (movedEntity.generation != 0)
		{
			assert(movedEntity.index < mRecords.size());

			EntityRecord &movedRec = mRecords[movedEntity.index];
			movedRec.archetypeID = rec.archetypeID; // stays the same
			movedRec.chunkIndex = rec.chunkIndex;
			movedRec.slotIndex = newSlot;
		}
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		rec.generation++;
		rec.archetypeID = INVALID_ARCHETYPE_ID;
		mFreeIndices.push_back(entity.index);

		entity.state = State::Destroyed;
	}

	Entity ECS::cloneEntity(const Entity &src, bool cloneHierarchy)
	{
		if (!alive(src))
		{
			return NULL_ENTITY;
		}

		assert(src.index < mRecords.size());

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const EntityRecord &srcRec{mRecords[src.index]};

		assert(srcRec.archetypeID < mArchetypePtrs.size());

		const Archetype *srcArch{mArchetypePtrs[srcRec.archetypeID].get()};

		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		const ComponentMask srcTags{srcArch->getTags(srcRec.chunkIndex, srcRec.slotIndex)};

		std::array<const void *, MAX_COMPONENTS> copyData{};
		copyData.fill(nullptr);

		srcArch->forEachComponent([&](ComponentTypeID compID) {
			const void *srcPtr{getComponentPtr(src, compID)};

			if (!srcPtr)
			{
				return; // shouldn't happen
			}

			assert(compID < ComponentInfos.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const ComponentInfo &info{ComponentInfos[compID]};

			void *mem = operator new(info.size, std::align_val_t(info.alignment));
			info.copyConstruct(mem, srcPtr);

			assert(compID < copyData.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			copyData[compID] = mem;
		});

		// Create a new entity (initially in the empty archetype)
		Entity dst = createEntity();

		// Move the new entity to the target archetype, constructing components from the copies
		moveEntity(dst, srcArch->getRegularMask(), copyData, {}, srcTags);

		// Recursively clone children if requested
		if (cloneHierarchy)
		{
			const std::vector<Entity> children{getChildren(src)};

			for (const Entity &child : children)
			{
				if (!alive(child))
				{
					continue;
				}

				const Entity childClone{cloneEntity(child, true)};

				setParent(childClone, dst);
			}
		}

		return dst;
	}

	bool ECS::alive(const Entity &entity) const noexcept
	{
		if (entity.index >= mRecords.size())
		{
			return false;
		}

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const EntityRecord &rec{mRecords[entity.index]};

		return rec.generation == entity.generation && entity.state == State::Active && rec.archetypeID != INVALID_ARCHETYPE_ID;
	}

	void ECS::compact()
	{
		for (auto &archPtr : mArchetypePtrs)
		{
			archPtr->compact(mRecords);
		}
	}

	void ECS::removeComponent(const Entity &entity, const ComponentTypeID compID)
	{
		if (!alive(entity))
		{
			return;
		}

		assert(compID < ComponentInfos.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const Dimensia::Registry::ComponentInfo &info{ComponentInfos[compID]};

		if (info.isTag)
		{
			assert(entity.index < mRecords.size());

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const EntityRecord &rec{mRecords[entity.index]};

			assert(rec.archetypeID < mArchetypePtrs.size());

			mArchetypePtrs[rec.archetypeID]->clearTag(rec.chunkIndex, rec.slotIndex, compID);
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

			return;
		}

		assert(entity.index < mRecords.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		assert(mRecords[entity.index].archetypeID < mArchetypePtrs.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const Archetype *srcArch{mArchetypePtrs[mRecords[entity.index].archetypeID].get()};
		const ComponentMask oldRegular{srcArch->getRegularMask()};

		bool present{};

		if (compID < LOWER_HALF_BIT_MASK)
		{
			present = (oldRegular.mLow & (1U << compID)) != 0;
		}
		else
		{
			present = (oldRegular.mHigh & (1U << (compID - LOWER_HALF_BIT_MASK))) != 0;
		}

		if (!present)
		{
			return;
		}

		ComponentMask newRegular{oldRegular};

		if (compID < LOWER_HALF_BIT_MASK)
		{
			newRegular.mLow &= ~(1U << compID);
		}
		else
		{
			newRegular.mHigh &= ~(1U << (compID - LOWER_HALF_BIT_MASK));
		}

		std::array<const void *, MAX_COMPONENTS> copyData{};
		std::array<void *, MAX_COMPONENTS> moveData{};

		copyData.fill(nullptr);
		moveData.fill(nullptr);

		moveEntity(entity, newRegular, copyData, moveData);
	}

	void ECS::invalidateQueries()
	{
		mQueryCache.clearResults();
		mMultiQueryCache.clear();
	}

	// MARK: Private Getters

	Archetype *ECS::getOrCreateArchetype(ComponentMask regularMask)
	{
		auto iterator{mArchetypeMaskToID.find(regularMask)};

		if (iterator != mArchetypeMaskToID.end())
		{
			assert(iterator->second < mArchetypePtrs.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			return mArchetypePtrs[iterator->second].get();
		}

		ui newID{static_cast<ui>(mArchetypePtrs.size())};
		auto newArch{std::make_unique<Archetype>(regularMask, newID)};
		Archetype *ptr{newArch.get()};

		mArchetypePtrs.push_back(std::move(newArch));
		mArchetypeMaskToID[regularMask] = newID;
		mQueryCache.addArchetype(regularMask, ptr);

		invalidateQueries();

		return ptr;
	}

	void *ECS::getComponentPtr(const Entity &entity, const ComponentTypeID compID)
	{
		if (!alive(entity))
		{
			return nullptr;
		}

		assert(entity.index < mRecords.size());

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const EntityRecord &rec{mRecords[entity.index]};

		assert(rec.archetypeID < mArchetypePtrs.size());

		const Archetype *arch{mArchetypePtrs[rec.archetypeID].get()};
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		const ComponentMask mask{arch->getRegularMask()};
		bool hasComp{};

		if (compID < LOWER_HALF_BIT_MASK)
		{
			hasComp = (mask.mLow & (1U << compID)) != 0;
		}
		else
		{
			hasComp = (mask.mHigh & (1U << (compID - LOWER_HALF_BIT_MASK))) != 0;
		}

		if (!hasComp)
		{
			return nullptr;
		}

		void *arr{arch->getComponentArray(rec.chunkIndex, compID)};

		if (arr == nullptr)
		{
			return nullptr;
		}

		assert(compID < ComponentInfos.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const std::size_t size{ComponentInfos[compID].size};

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		return static_cast<std::byte *>(arr) + (rec.slotIndex * size);
	}

	const void *ECS::getComponentPtr(const Entity &entity, const ComponentTypeID compID) const
	{
		if (!alive(entity))
		{
			return nullptr;
		}

		assert(entity.index < mRecords.size());

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const EntityRecord &rec{mRecords[entity.index]};

		assert(rec.archetypeID < mArchetypePtrs.size());

		const Archetype *arch{mArchetypePtrs[rec.archetypeID].get()};
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		const ComponentMask mask{arch->getRegularMask()};
		bool hasComp{};

		if (compID < LOWER_HALF_BIT_MASK)
		{
			hasComp = (mask.mLow & (1U << compID)) != 0;
		}
		else
		{
			hasComp = (mask.mHigh & (1U << (compID - LOWER_HALF_BIT_MASK))) != 0;
		}

		if (!hasComp)
		{
			return nullptr;
		}

		const void *arr{arch->getComponentArray(rec.chunkIndex, compID)};

		if (arr == nullptr)
		{
			return nullptr;
		}

		assert(compID < ComponentInfos.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const std::size_t size{ComponentInfos[compID].size};

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		return static_cast<const std::byte *>(arr) + (rec.slotIndex * size);
	}

	// MARK: Private Member Functions

	void ECS::moveEntity(const Entity &entity, ComponentMask newRegularMask, const std::array<const void *, MAX_COMPONENTS> &copyData,
						 const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask newTags)
	{
		assert(entity.index < mRecords.size());

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		EntityRecord &rec{mRecords[entity.index]};

		assert(rec.archetypeID < mArchetypePtrs.size());

		Archetype *srcArch{mArchetypePtrs[rec.archetypeID].get()};
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		ui srcChunk{rec.chunkIndex};
		ui srcSlot{rec.slotIndex};

		const ComponentMask oldRegular{srcArch->getRegularMask()};
		const ComponentMask oldTags{srcArch->getTags(srcChunk, srcSlot)};

		if (oldRegular == newRegularMask && oldTags == newTags)
		{
			return;
		}

		std::array<const void *, MAX_COMPONENTS> finalCopy{};
		std::array<void *, MAX_COMPONENTS> finalMove{};

		finalCopy.fill(nullptr);
		finalMove.fill(nullptr);

		forEachSetBit(oldRegular, [&](const ComponentTypeID componentTypeID) {
			assert(componentTypeID < finalMove.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			finalMove[componentTypeID] = getComponentPtr(entity, componentTypeID);
		});

		ComponentMask moveOverrideMask{0, 0};
		ComponentMask copyOverrideMask{0, 0};

		acquireOverrideMasks(copyData, moveData, moveOverrideMask, copyOverrideMask);

		forEachSetBit(moveOverrideMask, [&](ComponentTypeID componentTypeID) {
			assert(componentTypeID < finalMove.size());
			assert(componentTypeID < moveData.size());
			assert(componentTypeID < finalCopy.size());

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			finalMove[componentTypeID] = moveData[componentTypeID];
			finalCopy[componentTypeID] = nullptr;
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		});

		forEachSetBit(copyOverrideMask, [&](ComponentTypeID componentTypeID) {
			assert(componentTypeID < finalCopy.size());
			assert(componentTypeID < copyData.size());
			assert(componentTypeID < finalMove.size());

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			finalCopy[componentTypeID] = copyData[componentTypeID];
			finalMove[componentTypeID] = nullptr;
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		});

		Archetype *dstArch{getOrCreateArchetype(newRegularMask)};
		auto [newChunk, newSlot]{dstArch->addEntity(entity, finalCopy, finalMove, newTags)};
		auto [movedEntity, vacatedSlot]{srcArch->removeEntity(srcChunk, srcSlot)};

		rec.archetypeID = dstArch->getId();
		rec.chunkIndex = newChunk;
		rec.slotIndex = newSlot;

		if (movedEntity.generation != 0)
		{
			assert(movedEntity.index < mRecords.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			EntityRecord &movedRec = mRecords[movedEntity.index];
			movedRec.archetypeID = srcArch->getId();
			movedRec.chunkIndex = srcChunk;
			movedRec.slotIndex = vacatedSlot;
		}
	}

	void ECS::destroyHierarchy(const Entity &entity)
	{
		std::vector<Entity> childrenCopy;

		{
			const std::scoped_lock<std::mutex> lock(mHierarchyMutex);

			if (entity.index < mChildren.size())
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				childrenCopy = mChildren[entity.index];
			}
		}

		for (Entity &child : childrenCopy)
		{
			destroyEntity(child);
		}
	}
} // namespace Dimensia::ECS