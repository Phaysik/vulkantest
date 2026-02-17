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

	ECS::ECS()
		: records_(), freeIndices_(), nextEntityIndex_(0), archetypePtrs_(), archetypeMaskToId_(), queryCache_(), threadPool_(),
		  workStealingPool_(), hierarchyMutex_(), parent_(), children_()
	{
		getOrCreateArchetype(ComponentMask(0));
	}

	bool ECS::alive(Entity entity) const
	{
		if (entity.index >= records_.size())
		{
			return false;
		}
		const auto &rec = records_[entity.index];
		return rec.generation == entity.generation && rec.archetypeId != INVALID_ARCHETYPE_ID;
	}

	Entity ECS::createEntity()
	{
		uint32_t idx, gen;
		if (!freeIndices_.empty())
		{
			idx = freeIndices_.back();
			freeIndices_.pop_back();
			gen = records_[idx].generation;
		}
		else
		{
			idx = nextEntityIndex_++;
			records_.resize(idx + 1);
			gen = 1;
			records_[idx].generation = gen;
		}
		Entity e{idx, gen};

		Archetype *emptyArch = getOrCreateArchetype(ComponentMask(0));
		std::array<const void *, MAX_COMPONENTS> noCopy{};
		std::array<void *, MAX_COMPONENTS> noMove{};
		noCopy.fill(nullptr);
		noMove.fill(nullptr);
		auto [chunk, slot] = emptyArch->addEntity(e, noCopy, noMove, ComponentMask(0));
		records_[idx] = {gen, emptyArch->getId(), chunk, slot};

		const std::scoped_lock<std::mutex> lock(hierarchyMutex_);
		if (parent_.size() <= idx)
		{
			parent_.resize(idx + 1, NULL_ENTITY);
			children_.resize(idx + 1);
		}
		return e;
	}

	void ECS::destroyEntity(Entity entity, const bool destroyChildren)
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
			const std::scoped_lock<std::mutex> lock(hierarchyMutex_);
			if (entity.index < parent_.size() && parent_[entity.index] != NULL_ENTITY)
			{
				Entity parent = parent_[entity.index];
				if (parent.index < children_.size())
				{
					auto &siblings = children_[parent.index];
					siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());
				}
				parent_[entity.index] = NULL_ENTITY;
			}
			if (entity.index < children_.size())
			{
				children_[entity.index].clear();
			}
		}

		auto &rec = records_[entity.index];
		Archetype *arch = archetypePtrs_[rec.archetypeId].get();
		auto [movedEntity, newSlot] = arch->removeEntity(rec.chunkIndex, rec.slotIndex);
		if (movedEntity.generation != 0)
		{
			auto &movedRec = records_[movedEntity.index];
			movedRec.archetypeId = rec.archetypeId; // stays the same
			movedRec.chunkIndex = rec.chunkIndex;
			movedRec.slotIndex = newSlot;
		}
		rec.generation++;
		rec.archetypeId = INVALID_ARCHETYPE_ID;
		freeIndices_.push_back(entity.index);
	}

	void ECS::destroyHierarchy(Entity entity)
	{
		std::vector<Entity> childrenCopy;
		{
			const std::scoped_lock<std::mutex> lock(hierarchyMutex_);
			if (entity.index < children_.size())
			{
				childrenCopy = children_[entity.index];
			}
		}
		for (Entity child : childrenCopy)
		{
			destroyEntity(child);
		}
	}

	Archetype *ECS::getOrCreateArchetype(ComponentMask regularMask)
	{
		auto it = archetypeMaskToId_.find(regularMask);
		if (it != archetypeMaskToId_.end())
		{
			return archetypePtrs_[it->second].get();
		}

		uint32_t newId = static_cast<uint32_t>(archetypePtrs_.size());
		auto newArch = std::make_unique<Archetype>(regularMask, newId);
		Archetype *ptr = newArch.get();
		archetypePtrs_.push_back(std::move(newArch));
		archetypeMaskToId_[regularMask] = newId;
		queryCache_.addArchetype(regularMask, ptr);
		return ptr;
	}

	void *ECS::getComponentPtr(Entity entity, ComponentTypeID compId)
	{
		if (!alive(entity))
		{
			return nullptr;
		}
		auto &rec = records_[entity.index];
		Archetype *arch = archetypePtrs_[rec.archetypeId].get();
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

	const void *ECS::getComponentPtr(Entity entity, ComponentTypeID compId) const
	{
		if (!alive(entity))
		{
			return nullptr;
		}
		const auto &rec = records_[entity.index];
		Archetype *arch = archetypePtrs_[rec.archetypeId].get(); // const? Actually we need const access; but archetypePtrs_ is mutable?
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

	void ECS::removeComponent(Entity entity, ComponentTypeID compId)
	{
		if (!alive(entity))
		{
			return;
		}
		const auto &info = ComponentInfos[compId];
		if (info.isTag)
		{
			auto &rec = records_[entity.index];
			archetypePtrs_[rec.archetypeId]->clearTag(rec.chunkIndex, rec.slotIndex, compId);
			return;
		}

		Archetype *srcArch = archetypePtrs_[records_[entity.index].archetypeId].get();
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

	void ECS::moveEntity(Entity entity, ComponentMask newRegularMask, const std::array<const void *, MAX_COMPONENTS> &copyData,
						 const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask newTags)
	{
		auto &rec = records_[entity.index];
		Archetype *srcArch = archetypePtrs_[rec.archetypeId].get();
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
			auto &movedRec = records_[movedEntity.index];
			movedRec.archetypeId = srcArch->getId();
			movedRec.chunkIndex = srcChunk;
			movedRec.slotIndex = vacatedSlot;
		}
	}

	void ECS::setParent(Entity child, Entity parent)
	{
		if (!alive(child))
		{
			return;
		}
		if (parent != NULL_ENTITY && !alive(parent))
		{
			return;
		}

		const std::scoped_lock<std::mutex> lock(hierarchyMutex_);
		uint32_t maxIdx = std::max(child.index, parent.index);
		if (parent_.size() <= maxIdx)
		{
			parent_.resize(maxIdx + 1, NULL_ENTITY);
			children_.resize(maxIdx + 1);
		}

		Entity oldParent = (child.index < parent_.size()) ? parent_[child.index] : NULL_ENTITY;
		if (oldParent == parent)
		{
			return;
		}

		if (oldParent != NULL_ENTITY && oldParent.index < children_.size())
		{
			auto &siblings = children_[oldParent.index];
			siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
		}
		parent_[child.index] = parent;
		if (parent != NULL_ENTITY)
		{
			if (parent.index >= children_.size())
			{
				children_.resize(parent.index + 1);
			}
			children_[parent.index].push_back(child);
		}
	}

	std::vector<Entity> ECS::getChildren(Entity parent) const
	{
		const std::scoped_lock<std::mutex> lock(hierarchyMutex_);
		if (parent.index < children_.size())
		{
			return children_[parent.index];
		}
		return {};
	}

	Entity ECS::getParent(Entity child) const
	{
		const std::scoped_lock<std::mutex> lock(hierarchyMutex_);
		if (child.index < parent_.size())
		{
			return parent_[child.index];
		}
		return NULL_ENTITY;
	}

	void ECS::compact()
	{
		for (auto &archPtr : archetypePtrs_)
		{
			archPtr->compact(records_);
		}
	}
} // namespace Dimensia::ECS