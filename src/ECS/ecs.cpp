/*! \file ecs.cpp
	\brief Contains the function definitions for creating a ecs
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/ecs.h"

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

	void ECS::setParent(const Entity &child, Entity parent)
	{
		if (!alive(child))
		{
			return;
		}
		if (parent != NULL_ENTITY && !alive(parent))
		{
			return;
		}

		const std::scoped_lock<std::mutex> lock(mHierarchyMutex);
		uint32_t maxIdx = std::max(child.index, parent.index);
		if (mParent.size() <= maxIdx)
		{
			mParent.resize(maxIdx + 1, NULL_ENTITY);
			mChildren.resize(maxIdx + 1);
		}

		Entity oldParent = (child.index < mParent.size()) ? mParent[child.index] : NULL_ENTITY;
		if (oldParent == parent)
		{
			return;
		}

		if (oldParent != NULL_ENTITY && oldParent.index < mChildren.size())
		{
			auto &siblings = mChildren[oldParent.index];
			siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
		}
		mParent[child.index] = parent;
		if (parent != NULL_ENTITY)
		{
			if (parent.index >= mChildren.size())
			{
				mChildren.resize(parent.index + 1);
			}
			mChildren[parent.index].push_back(child);
		}
	}

	// MARK: Member Functions

	Entity ECS::createEntity()
	{
		uint32_t idx, gen;
		if (!mFreeIndices.empty())
		{
			idx = mFreeIndices.back();
			mFreeIndices.pop_back();
			gen = mRecords[idx].generation;
		}
		else
		{
			idx = mNextEntityIndex++;
			mRecords.resize(idx + 1);
			gen = 1;
			mRecords[idx].generation = gen;
		}
		Entity e{idx, gen};

		Archetype *emptyArch = getOrCreateArchetype(ComponentMask(0));
		std::array<const void *, MAX_COMPONENTS> noCopy{};
		std::array<void *, MAX_COMPONENTS> noMove{};
		noCopy.fill(nullptr);
		noMove.fill(nullptr);
		auto [chunk, slot] = emptyArch->addEntity(e, noCopy, noMove, ComponentMask(0));
		mRecords[idx] = {gen, emptyArch->getId(), chunk, slot};

		const std::scoped_lock<std::mutex> lock(mHierarchyMutex);
		if (mParent.size() <= idx)
		{
			mParent.resize(idx + 1, NULL_ENTITY);
			mChildren.resize(idx + 1);
		}
		return e;
	}

	void ECS::destroyEntity(const Entity &entity, const bool destroyChildren)
	{
		if (!alive(entity))
		{
			return;
		}
		if (destroyChildren)
		{
			destroyHierarchy(entity);
		}

		{
			const std::scoped_lock<std::mutex> lock(mHierarchyMutex);
			if (entity.index < mParent.size() && mParent[entity.index] != NULL_ENTITY)
			{
				Entity parent = mParent[entity.index];
				if (parent.index < mChildren.size())
				{
					auto &siblings = mChildren[parent.index];
					siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());
				}
				mParent[entity.index] = NULL_ENTITY;
			}
			if (entity.index < mChildren.size())
			{
				mChildren[entity.index].clear();
			}
		}

		auto &rec = mRecords[entity.index];
		Archetype *arch = mArchetypePtrs[rec.archetypeId].get();
		auto [movedEntity, newSlot] = arch->removeEntity(rec.chunkIndex, rec.slotIndex);
		if (movedEntity.generation != 0)
		{
			auto &movedRec = mRecords[movedEntity.index];
			movedRec.archetypeId = rec.archetypeId; // stays the same
			movedRec.chunkIndex = rec.chunkIndex;
			movedRec.slotIndex = newSlot;
		}
		rec.generation++;
		rec.archetypeId = INVALID_ARCHETYPE_ID;
		mFreeIndices.push_back(entity.index);
	}

	bool ECS::alive(const Entity &entity) const
	{
		if (entity.index >= mRecords.size())
		{
			return false;
		}
		const auto &rec = mRecords[entity.index];
		return rec.generation == entity.generation && rec.archetypeId != INVALID_ARCHETYPE_ID;
	}

	void ECS::compact()
	{
		for (auto &archPtr : mArchetypePtrs)
		{
			archPtr->compact(mRecords);
		}
	}

	void ECS::removeComponent(const Entity &entity, ComponentTypeID compId)
	{
		if (!alive(entity))
		{
			return;
		}
		const auto &info = ComponentInfos[compId];
		if (info.isTag)
		{
			auto &rec = mRecords[entity.index];
			mArchetypePtrs[rec.archetypeId]->clearTag(rec.chunkIndex, rec.slotIndex, compId);
			return;
		}

		Archetype *srcArch = mArchetypePtrs[mRecords[entity.index].archetypeId].get();
		ComponentMask oldRegular = srcArch->getRegularMask();
		bool present;
		if (compId < 64)
		{
			present = (oldRegular.mLow & (uint64_t(1) << compId)) != 0;
		}
		else
		{
			present = (oldRegular.mHigh & (uint64_t(1) << (compId - 64))) != 0;
		}
		if (!present)
		{
			return;
		}

		ComponentMask newRegular = oldRegular;
		if (compId < 64)
		{
			newRegular.mLow &= ~(uint64_t(1) << compId);
		}
		else
		{
			newRegular.mHigh &= ~(uint64_t(1) << (compId - 64));
		}

		std::array<const void *, MAX_COMPONENTS> copyData{};
		std::array<void *, MAX_COMPONENTS> moveData{};
		copyData.fill(nullptr);
		moveData.fill(nullptr);
		moveEntity(entity, newRegular, copyData, moveData);
	}

	// MARK: Private Getters

	Archetype *ECS::getOrCreateArchetype(ComponentMask regularMask)
	{
		auto it = mArchetypeMaskToID.find(regularMask);
		if (it != mArchetypeMaskToID.end())
		{
			return mArchetypePtrs[it->second].get();
		}

		uint32_t newId = static_cast<uint32_t>(mArchetypePtrs.size());
		auto newArch = std::make_unique<Archetype>(regularMask, newId);
		Archetype *ptr = newArch.get();
		mArchetypePtrs.push_back(std::move(newArch));
		mArchetypeMaskToID[regularMask] = newId;
		mQueryCache.addArchetype(regularMask, ptr);
		return ptr;
	}

	void *ECS::getComponentPtr(const Entity &entity, ComponentTypeID compId)
	{
		if (!alive(entity))
		{
			return nullptr;
		}
		auto &rec = mRecords[entity.index];
		Archetype *arch = mArchetypePtrs[rec.archetypeId].get();
		ComponentMask mask = arch->getRegularMask();
		bool hasComp;
		if (compId < 64)
		{
			hasComp = (mask.mLow & (uint64_t(1) << compId)) != 0;
		}
		else
		{
			hasComp = (mask.mHigh & (uint64_t(1) << (compId - 64))) != 0;
		}
		if (!hasComp)
		{
			return nullptr;
		}
		void *arr = arch->getComponentArray(rec.chunkIndex, compId);
		if (!arr)
		{
			return nullptr;
		}
		std::size_t size = ComponentInfos[compId].size;
		return static_cast<std::byte *>(arr) + rec.slotIndex * size;
	}

	const void *ECS::getComponentPtr(const Entity &entity, ComponentTypeID compId) const
	{
		if (!alive(entity))
		{
			return nullptr;
		}
		const auto &rec = mRecords[entity.index];
		Archetype *arch = mArchetypePtrs[rec.archetypeId].get(); // const? Actually we need const access; but archetypePtrs_ is mutable?
																 // We'll keep it as non-const because we're not modifying.
		ComponentMask mask = arch->getRegularMask();
		bool hasComp;
		if (compId < 64)
		{
			hasComp = (mask.mLow & (uint64_t(1) << compId)) != 0;
		}
		else
		{
			hasComp = (mask.mHigh & (uint64_t(1) << (compId - 64))) != 0;
		}
		if (!hasComp)
		{
			return nullptr;
		}
		const void *arr = arch->getComponentArray(rec.chunkIndex, compId);
		if (!arr)
		{
			return nullptr;
		}
		std::size_t size = ComponentInfos[compId].size;
		return static_cast<const std::byte *>(arr) + rec.slotIndex * size;
	}

	// MARK: Private Member Functions

	void ECS::moveEntity(const Entity &entity, ComponentMask newRegularMask, const std::array<const void *, MAX_COMPONENTS> &copyData,
						 const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask newTags)
	{
		auto &rec = mRecords[entity.index];
		Archetype *srcArch = mArchetypePtrs[rec.archetypeId].get();
		uint32_t srcChunk = rec.chunkIndex;
		uint32_t srcSlot = rec.slotIndex;

		ComponentMask oldRegular = srcArch->getRegularMask();
		ComponentMask oldTags = srcArch->getTags(srcChunk, srcSlot);

		if (oldRegular == newRegularMask && oldTags == newTags)
		{
			return;
		}

		std::array<const void *, MAX_COMPONENTS> finalCopy{};
		std::array<void *, MAX_COMPONENTS> finalMove{};
		finalCopy.fill(nullptr);
		finalMove.fill(nullptr);

		forEachSetBit(oldRegular, [&](ComponentTypeID id) { finalMove[id] = getComponentPtr(entity, id); });

		ComponentMask moveOverrideMask{0, 0}, copyOverrideMask{0, 0};
		for (ComponentTypeID id = 0; id < MAX_COMPONENTS; ++id)
		{
			if (moveData[id] != nullptr)
			{
				if (id < 64)
				{
					moveOverrideMask.mLow |= (uint64_t(1) << id);
				}
				else
				{
					moveOverrideMask.mHigh |= (uint64_t(1) << (id - 64));
				}
			}
			else if (copyData[id] != nullptr)
			{
				if (id < 64)
				{
					copyOverrideMask.mLow |= (uint64_t(1) << id);
				}
				else
				{
					copyOverrideMask.mHigh |= (uint64_t(1) << (id - 64));
				}
			}
		}

		forEachSetBit(moveOverrideMask, [&](ComponentTypeID id) {
			finalMove[id] = moveData[id];
			finalCopy[id] = nullptr;
		});
		forEachSetBit(copyOverrideMask, [&](ComponentTypeID id) {
			finalCopy[id] = copyData[id];
			finalMove[id] = nullptr;
		});

		Archetype *dstArch = getOrCreateArchetype(newRegularMask);
		auto [newChunk, newSlot] = dstArch->addEntity(entity, finalCopy, finalMove, newTags);
		auto [movedEntity, vacatedSlot] = srcArch->removeEntity(srcChunk, srcSlot);

		rec.archetypeId = dstArch->getId();
		rec.chunkIndex = newChunk;
		rec.slotIndex = newSlot;

		if (movedEntity.generation != 0)
		{
			auto &movedRec = mRecords[movedEntity.index];
			movedRec.archetypeId = srcArch->getId();
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
				childrenCopy = mChildren[entity.index];
			}
		}
		for (Entity child : childrenCopy)
		{
			destroyEntity(child);
		}
	}
} // namespace Dimensia::ECS