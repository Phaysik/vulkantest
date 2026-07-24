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
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ECS/archetype.h"
#include "ECS/componentMask.h"
#include "ECS/componentRegistry.h"
#include "ECS/entity.h"
#include "ECS/entityRecord.h"
#include "ECS/processChunkHelpers.h"
#include "ECS/systemVersion.h"

namespace Dimensia::ECS
{
	// MARK: Constructor

	ECS::ECS(const EntityInsertionHook insertionHook)
		: mEntityInsertionHook(insertionHook), mRecords(0), mFreeIndices(0), mArchetypePtrs(0), mParent(0), mChildren(0)
	{
		getOrCreateArchetype(ComponentMask(0));
	}

	// MARK: Getters

	std::vector<Entity> ECS::getChildren(const Entity &parent) const
	{
		if (!alive(parent))
		{
			return {};
		}

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
		if (!alive(child))
		{
			return NULL_ENTITY;
		}

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
		const StructuralGuard structuralGuard{mStructuralMutex};

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
		const ui maxIdx{(parent != NULL_ENTITY) ? std::max(child.index, parent.index) : child.index};

		if (mParent.size() <= maxIdx)
		{
			mParent.resize(maxIdx + 1, NULL_ENTITY);
			mChildren.resize(maxIdx + 1);
		}

		if (wouldCreateHierarchyCycle(child, parent))
		{
			return;
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

	bool ECS::wouldCreateHierarchyCycle(const Entity &child, const Entity &parent) const
	{
		Entity ancestor{parent};
		std::size_t visitedCount{};
		while (ancestor != NULL_ENTITY && visitedCount <= mParent.size())
		{
			if (ancestor == child)
			{
				return true;
			}

			if (!alive(ancestor) || ancestor.index >= mParent.size())
			{
				return false;
			}

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			ancestor = mParent[ancestor.index];
			++visitedCount;
		}

		return visitedCount > mParent.size();
	}

	// MARK: Member Functions

	Entity ECS::allocateEntityID()
	{
		const bool reuseIndex{!mFreeIndices.empty()};
		const ui index{reuseIndex ? mFreeIndices.back() : mNextEntityIndex};

		if (!reuseIndex)
		{
			if (mNextEntityIndex == UINT32_MAX)
			{
				throw std::overflow_error("ECS entity index space exhausted");
			}

			mRecords.resize(static_cast<std::size_t>(index) + 1);
		}

		assert(index < mRecords.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const ui generation{reuseIndex ? mRecords[index].generation : 1};

		const std::scoped_lock<std::mutex> lock(mHierarchyMutex);

		if (mParent.size() <= index)
		{
			mParent.resize(index + 1, NULL_ENTITY);
			mChildren.resize(index + 1);
		}

		if (reuseIndex)
		{
			mFreeIndices.pop_back();
		}
		else
		{
			++mNextEntityIndex;

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			mRecords[index].generation = generation;
		}

		return Entity{.index = index, .generation = generation};
	}

	void ECS::releaseReservedEntityID(const Entity &entity) noexcept
	{
		assert(entity.index < mRecords.size());

		// A newly issued top index can be rewound without touching the free list.
		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		if (mRecords[entity.index].state == State::Uninitialized && entity.index + 1 == mNextEntityIndex)
		{
			--mNextEntityIndex;
			mRecords.pop_back();
			return;
		}

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mRecords[entity.index] = {
			.generation = entity.generation,
			.archetypeID = INVALID_ARCHETYPE_ID,
			.chunkIndex = 0,
			.slotIndex = 0,
			.state = State::Destroyed,
		};
		mFreeIndices.push_back(entity.index);
	}

	Entity ECS::createEntity()
	{
		const StructuralGuard structuralGuard{mStructuralMutex};
		const VersionType version{nextVersion()};
		Entity entity{allocateEntityID()};

		try
		{
			mEntityInsertionHook();
			Archetype *const emptyArch{getOrCreateArchetype(ComponentMask(0))};
			std::array<const void *, MAX_COMPONENTS> noCopy{};
			std::array<void *, MAX_COMPONENTS> noMove{};

			noCopy.fill(nullptr);
			noMove.fill(nullptr);

			auto [chunk, slot]{emptyArch->addEntity(entity, noCopy, noMove, ComponentMask(0), version)};

			assert(entity.index < mRecords.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			mRecords[entity.index] = {
				.generation = entity.generation,
				.archetypeID = emptyArch->getId(),
				.chunkIndex = chunk,
				.slotIndex = slot,
				.state = State::Active,
			};
		}
		catch (...)
		{
			releaseReservedEntityID(entity);
			throw;
		}

		return entity;
	}

	void ECS::destroyEntity(Entity &entity, const bool destroyChildren)
	{
		const StructuralGuard structuralGuard{mStructuralMutex};
		if (!alive(entity))
		{
			return;
		}

		assert(entity.index < mRecords.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mRecords[entity.index].state = State::Destroying;

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
				// Clear stale parent handles on orphaned children to prevent dangling references
				for (const Entity &child : mChildren[entity.index])
				{
					if (child.index < mParent.size())
					{
						mParent[child.index] = NULL_ENTITY;
					}
				}

				mChildren[entity.index].clear();
			}
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		}

		assert(entity.index < mRecords.size());

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		EntityRecord &rec{mRecords[entity.index]};

		assert(rec.archetypeID < mArchetypePtrs.size());

		Archetype *const arch{mArchetypePtrs[rec.archetypeID].get()};

		auto [movedEntity, newSlot]{arch->removeEntity(rec.chunkIndex, rec.slotIndex, nextVersion())};

		if (movedEntity != NULL_ENTITY)
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
		rec.state = State::Destroyed;
		mFreeIndices.push_back(entity.index);
	}

	Entity ECS::cloneEntity(const Entity &src, bool cloneHierarchy)
	{
		const StructuralGuard structuralGuard{mStructuralMutex};
		if (!alive(src))
		{
			return NULL_ENTITY;
		}

		assert(src.index < mRecords.size());

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const EntityRecord srcRec{mRecords[src.index]};

		assert(srcRec.archetypeID < mArchetypePtrs.size());

		Archetype *srcArch{mArchetypePtrs[srcRec.archetypeID].get()};

		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		const ComponentMask srcTags{srcArch->getTags(srcRec.chunkIndex, srcRec.slotIndex)};

		std::array<const void *, MAX_COMPONENTS> copyData{};
		std::array<void *, MAX_COMPONENTS> noMove{};
		copyData.fill(nullptr);
		noMove.fill(nullptr);

		srcArch->forEachComponent([&](ComponentTypeID compID) {
			assert(compID < copyData.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			copyData[compID] = getComponentPtr(src, compID);
		});

		Entity dst{allocateEntityID()};
		try
		{
			mEntityInsertionHook();
			auto [chunk, slot]{srcArch->addEntity(dst, copyData, noMove, srcTags, nextVersion())};

			assert(dst.index < mRecords.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			mRecords[dst.index] = {
				.generation = dst.generation,
				.archetypeID = srcArch->getId(),
				.chunkIndex = chunk,
				.slotIndex = slot,
				.state = State::Active,
			};
		}
		catch (...)
		{
			releaseReservedEntityID(dst);
			throw;
		}

		try
		{
			if (cloneHierarchy)
			{
				const std::vector<Entity> children{getChildren(src)};

				for (const Entity &child : children)
				{
					if (!alive(child))
					{
						continue;
					}

					Entity childClone{cloneEntity(child, true)};

					try
					{
						setParent(childClone, dst);
					}
					catch (...)
					{
						destroyEntity(childClone, true);
						throw;
					}
				}
			}
		}
		catch (...)
		{
			destroyEntity(dst, true);
			throw;
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

		return rec.generation == entity.generation && rec.state == State::Active && rec.archetypeID != INVALID_ARCHETYPE_ID;
	}

	bool ECS::archetypeIsEmpty(const Archetype &archetype) noexcept
	{
		for (ui chunk{0}; chunk < archetype.getChunkCount(); ++chunk)
		{
			if (archetype.getEntityCount(chunk) > 0)
			{
				return false;
			}
		}

		return true;
	}

	void ECS::updateArchetypeEntityRecords(const Archetype &archetype, const ui archetypeID)
	{
		for (ui chunk{0}; chunk < archetype.getChunkCount(); ++chunk)
		{
			const ui entityCount{archetype.getEntityCount(chunk)};
			const Entity *entities{archetype.getEntityArray(chunk)};
			for (ui slot{0}; slot < entityCount; ++slot)
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
				assert(entities[slot].index < mRecords.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				mRecords[entities[slot].index].archetypeID = archetypeID;
			}
		}
	}

	void ECS::pruneEmptyArchetypes()
	{
		for (std::size_t i{mArchetypePtrs.size()}; i > 1; --i)
		{
			const std::size_t idx{i - 1};
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			const Archetype *archetype{mArchetypePtrs[idx].get()};
			if (!archetypeIsEmpty(*archetype))
			{
				continue;
			}

			mQueryCache.removeArchetype(archetype);
			mArchetypeMaskToID.erase(archetype->getRegularMask());

			const std::size_t lastIndex{mArchetypePtrs.size() - 1};
			if (idx != lastIndex)
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				std::swap(mArchetypePtrs[idx], mArchetypePtrs[lastIndex]);

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				Archetype &movedArchetype{*mArchetypePtrs[idx]};
				movedArchetype.setId(static_cast<ui>(idx));
				mArchetypeMaskToID[movedArchetype.getRegularMask()] = static_cast<ui>(idx);
				updateArchetypeEntityRecords(movedArchetype, static_cast<ui>(idx));
			}

			mArchetypePtrs.pop_back();
		}
	}

	void ECS::compactHierarchyStorage()
	{
		const std::scoped_lock<std::mutex> lock(mHierarchyMutex);
		ui maxAliveIndex{0};
		bool hasAlive{false};

		for (ui index{0}; index < static_cast<ui>(mRecords.size()); ++index)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			if (mRecords[index].state == State::Active)
			{
				maxAliveIndex = index;
				hasAlive = true;
			}
		}

		const std::size_t newSize{hasAlive ? static_cast<std::size_t>(maxAliveIndex) + 1 : 0};
		if (newSize >= mParent.size())
		{
			return;
		}

		mParent.resize(newSize);
		mParent.shrink_to_fit();
		mChildren.resize(newSize);
		mChildren.shrink_to_fit();
	}

	void ECS::compact()
	{
		const StructuralGuard structuralGuard{mStructuralMutex};
		for (auto &archPtr : mArchetypePtrs)
		{
			archPtr->compact(mRecords);
		}

		pruneEmptyArchetypes();
		compactHierarchyStorage();

		invalidateQueries();
	}

	void ECS::removeComponent(const Entity &entity, const ComponentTypeID compID)
	{
		const StructuralGuard structuralGuard{mStructuralMutex};
		if (compID >= ComponentInfos.size())
		{
			throw std::out_of_range("Invalid ECS component type ID");
		}

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

			mArchetypePtrs[rec.archetypeID]->clearTag(rec.chunkIndex, rec.slotIndex, compID, nextVersion());
			// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

			return;
		}

		assert(entity.index < mRecords.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		assert(mRecords[entity.index].archetypeID < mArchetypePtrs.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const Archetype *srcArch{mArchetypePtrs[mRecords[entity.index].archetypeID].get()};
		const ComponentMask oldRegular{srcArch->getRegularMask()};

		if (!oldRegular.testBit(compID))
		{
			return;
		}

		ComponentMask newRegular{oldRegular};

		newRegular.clearBit(compID);

		std::array<const void *, MAX_COMPONENTS> copyData{};
		std::array<void *, MAX_COMPONENTS> moveData{};

		copyData.fill(nullptr);
		moveData.fill(nullptr);

		moveEntity(entity, newRegular, copyData, moveData);
	}

	void ECS::invalidateQueries()
	{
		const StructuralGuard structuralGuard{mStructuralMutex};
		mQueryCache.clearResults();

		const std::unique_lock lock(mMultiQueryMutex);
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

		const ui newID{static_cast<ui>(mArchetypePtrs.size())};
		auto newArch{std::make_unique<Archetype>(regularMask, newID)};
		Archetype *const ptr{newArch.get()};

		mArchetypePtrs.push_back(std::move(newArch));
		mArchetypeMaskToID[regularMask] = newID;
		mQueryCache.addArchetype(regularMask, ptr);

		// Incrementally update mMultiQueryCache: add the new archetype to any
		// cached multi-clause query whose masks it satisfies.
		{
			const std::unique_lock lock(mMultiQueryMutex);
			for (auto &[key, results] : mMultiQueryCache)
			{
				// NOLINTNEXTLINE(readability-redundant-parentheses)
				if ((regularMask & key.required) != key.required)
				{
					continue;
				}

				// NOLINTNEXTLINE(readability-redundant-parentheses)
				if (key.any && !key.anyTags && !(regularMask & key.any))
				{
					continue;
				}

				// NOLINTNEXTLINE(readability-redundant-parentheses)
				if (key.none && (regularMask & key.none))
				{
					continue;
				}

				results.push_back(ptr);
			}
		}

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

		if (!mask.testBit(compID))
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

		if (!mask.testBit(compID))
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
						 const std::array<void *, MAX_COMPONENTS> &moveData, const std::optional<ComponentMask> newTags)
	{
		assert(entity.index < mRecords.size());

		// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		EntityRecord &rec{mRecords[entity.index]};

		assert(rec.archetypeID < mArchetypePtrs.size());

		Archetype *srcArch{mArchetypePtrs[rec.archetypeID].get()};
		// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

		const ui srcChunk{rec.chunkIndex};
		const ui srcSlot{rec.slotIndex};

		const ComponentMask oldRegular{srcArch->getRegularMask()};
		const ComponentMask oldTags{srcArch->getTags(srcChunk, srcSlot)};
		const ComponentMask targetTags{newTags.value_or(oldTags)};

		if (oldRegular == newRegularMask && oldTags == targetTags)
		{
			return;
		}

		std::array<const void *, MAX_COMPONENTS> finalCopy{};
		std::array<void *, MAX_COMPONENTS> finalMove{};

		finalCopy.fill(nullptr);
		finalMove.fill(nullptr);

		forEachSetBit(oldRegular & newRegularMask, [&](const ComponentTypeID componentTypeID) {
			assert(componentTypeID < finalCopy.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			finalCopy[componentTypeID] = getComponentPtr(entity, componentTypeID);
		});

		ComponentMask moveOverrideMask{0, 0};
		ComponentMask copyOverrideMask{0, 0};

		acquireOverrideMasks(copyData, moveData, moveOverrideMask, copyOverrideMask, newRegularMask);

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

		Archetype *const dstArch{getOrCreateArchetype(newRegularMask)};
		const VersionType version{nextVersion()};
		auto [newChunk, newSlot]{dstArch->addEntity(entity, finalCopy, finalMove, targetTags, version)};
		auto [movedEntity, vacatedSlot]{srcArch->removeEntity(srcChunk, srcSlot, version)};

		rec.archetypeID = dstArch->getId();
		rec.chunkIndex = newChunk;
		rec.slotIndex = newSlot;

		if (movedEntity != NULL_ENTITY)
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