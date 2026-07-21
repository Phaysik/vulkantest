/*! \file ecs.h
	\brief Core Entity-Component-System API and iteration utilities.
	\details This header defines the `ECS` class which is the central manager for entities, components, archetypes and queries in the
   Dimensia engine. The `ECS` implements an archetype-based storage model, provides fast query caching (`QueryCache`), and multiple
   execution policies for running systems over matching entities (sequential, parallel, batched, and work-stealing).
	@author Matthew Moore
	@date 02/25/2026
	@version x.x.x
*/

#ifndef INCLUDE_ECS_ECS_H
#define INCLUDE_ECS_ECS_H

#include <algorithm>
#include <atomic>
#include <latch>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/cconcepts.h"
#include "Core/typedefs.h"
#include "ECS/constants.h"
#include "Threading/threadPool.h"
#include "Threading/workStealingPool.h"

#include "archetype.h"
#include "commandBuffer.h"
#include "componentMask.h"
#include "componentRegistry.h"
#include "entity.h"
#include "entityRecord.h"
#include "processChunkHelpers.h"
#include "queryCache.h"
#include "queryFilter.h"
#include "systemVersion.h"

namespace Dimensia::ECS
{
	using Dimensia::Registry::ComponentInfo;
	using Dimensia::Registry::ComponentTypeID;
	using Dimensia::Registry::MAX_COMPONENTS;

	using Dimensia::Threading::ThreadPool;
	using Dimensia::Threading::WorkStealingPool;

	using Dimensia::Core::InvocableWithArgs;

	using Dimensia::Core::ui;

	enum class ExecutionPolicy : Dimensia::Core::ub
	{
		Seq,
		Par,
		ParBatched,
		ParStealing
	};

	/*! @class ECS include/ECS/ecs.h
		@brief Central Entity-Component-System managing entities, components, and queries.
		@details Provides creation/destruction of entities, component and tag management, query-driven iteration with configurable execution
	   policies, and versioned processing for systems. Threading pools are used for parallel execution paths. Caller is responsible for
	   valid entity handles.
		@note Thread-safety: many operations assume external synchronization for concurrent writes; read-only forEach variants are
	   thread-safe when using parallel execution policies.
		@author Matthew Moore
	*/
	class ECS
	{
		public:
			// MARK: Constructor, Assignment Operators, and Destructor

			/*! @brief Constructs a new empty ECS instance.
				@details Initializes internal pools, registries, and caches to default state.
				@post The ECS is ready to create entities and register components.
				@author Matthew Moore
			*/
			explicit ECS();

			/*! @brief Copy constructor (deleted).
				@details `ECS` owns non-copyable resources (thread pools, unique archetype storage). Copying would result in shallow copies
			   and double ownership; therefore the copy constructor is explicitly deleted to enforce single ownership semantics.
			*/
			ECS(const ECS &) = delete;

			/*! @brief Copy assignment operator (deleted).
				@details Prevents assigning one `ECS` to another. Assignment would imply transferring or duplicating internal ownership,
			   which is unsupported and unsafe for the contained resources.
			*/
			ECS &operator=(const ECS &) = delete;

			/*! @brief Move constructor (deleted).
				@details Moving an `ECS` would transfer ownership of internal pools and archetypes. To avoid subtle lifetime and concurrency
			   issues (dangling references, moved-from pools), move construction is disallowed; clients should manage a single `ECS`
			   instance instead.
			*/
			ECS(ECS &&) = delete;

			/*! @brief Move assignment operator (deleted).
				@details Move-assigning an `ECS` is disallowed for the same reasons as move construction: the class holds resources that
			   must not be implicitly transferred or invalidated by moves.
			*/
			ECS &operator=(ECS &&) = delete;

			// NOLINTBEGIN(hicpp-use-equals-default,modernize-use-equals-default)

			/*! @brief Destroys the ECS instance and associated resources.
				@details Cleans up archetypes and thread pools. Users should ensure entities are properly destroyed before ECS destruction
			   to avoid undefined behavior.
			*/
			~ECS() {}

			// NOLINTEND(hicpp-use-equals-default,modernize-use-equals-default)

			// MARK: Getters

			/*! @brief Returns the direct children of @p parent.
				@param[in] parent The parent entity.
				@return Vector of child entities; empty if none or if @p parent is not alive.
			*/
			std::vector<Entity> getChildren(const Entity &parent) const;

			/*! @brief Returns the parent of @p child.
				@param[in] child The child entity.
				@return The parent `Entity` handle, or an invalid/default entity if no parent exists.
			*/
			Entity getParent(const Entity &child) const;

			/*! @brief Computes an adaptive batch size for parallel processing.
				@param[in] allChunkSize Number of chunks to process.
				@return Calculated batch size (at least 1) based on hardware concurrency.
			*/
			static std::size_t getBatchSize(const std::size_t allChunkSize) noexcept
			{
				// Adaptive batch size based on hardware concurrency
				const std::size_t numThreads{std::thread::hardware_concurrency()};
				const std::size_t targetTasks{numThreads * 4};

				const std::size_t batchSize{(allChunkSize + targetTasks - 1) / targetTasks};

				return std::max<std::size_t>(batchSize, 1);
			}

			// MARK: Setter

			/*! @brief Sets or clears the parent relationship for @p child.
				@param[in] child The entity whose parent is to be updated.
				@param[in] parent The parent to set; an invalid entity may be used to clear the parent.
			*/
			void setParent(const Entity &child, const Entity &parent);

			// MARK: Member Functions

			/*! @brief Creates a new entity and returns a handle.
				@return A newly created `Entity`.
			*/
			Entity createEntity();

			/*! @brief Destroys @p entity and optionally its child hierarchy.
				@param[in] entity The entity to destroy.
				@param[in] destroyChildren If true, recursively destroys children; otherwise children are orphaned.
			*/
			void destroyEntity(Entity &entity, const bool destroyChildren = true);

			/*! @brief Create a new entity that is a copy of @p src.
				@param[in] src The source entity to clone. Must be alive.
				@param[in] cloneHierarchy If true, recursively clone all children and set parent relationships.
				@return A new entity handle with identical component values (and optionally same hierarchy).
			*/
			Entity cloneEntity(const Entity &src, bool cloneHierarchy = false);

			/*! @brief Returns whether @p entity refers to a currently alive entity.
				@param[in] entity The entity handle to test.
				@return True if alive, false otherwise.
			*/
			bool alive(const Entity &entity) const noexcept;

			/*! @brief Performs internal compaction to reclaim storage and defragment data structures.
			 */
			void compact();

			/*! @brief Removes the component identified by @p compID from @p entity.
				@param[in] entity The entity to update.
				@param[in] compID Component type identifier to remove.
			*/
			void removeComponent(const Entity &entity, const ComponentTypeID compID);

			void invalidateQueries();

			// MARK: Template Member Functions

			/*! @brief Create an entity and emplace provided components/tags.
				@tparam Ts Types of components or tags to attach.
				@param components Forwarded component values.
				@return Newly created `Entity` with components attached where applicable.
				@note Preserves move semantics for rvalue arguments.
			*/
			template <typename... Ts>
			Entity createEntityWith(Ts &&...components)
			{
				std::array<ComponentTypeID, sizeof...(Ts)> compIds{componentID<std::decay_t<Ts>>()...};

				ComponentMask regularMask{0, 0};
				ComponentMask tagMask{0, 0};

				std::array<const void *, MAX_COMPONENTS> copyData{};
				std::array<void *, MAX_COMPONENTS> moveData{};

				copyData.fill(nullptr);
				moveData.fill(nullptr);

				// Store forwarded component values in a tuple so we can safely take
				// addresses and preserve move semantics for rvalues.
				std::tuple<std::decay_t<Ts>...> storage{std::forward<Ts>(components)...};

				[&]<std::size_t... I>(std::index_sequence<I...>) {
					(([&] {
						 assert(I < compIds.size());

						 // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
						 const ComponentTypeID componentTypeID{compIds[I]};

						 assert(componentTypeID < ComponentInfos.size());

						 processCreateComponent<decltype(components)>(componentTypeID, copyData, moveData, regularMask, tagMask,
																	  std::get<I>(storage));
					 }()),
					 ...);
				}(std::index_sequence_for<Ts...>{});

				// Allocate entity ID without placing in any archetype
				Entity entity{allocateEntityID()};

				// Place directly in the target archetype, bypassing the empty archetype
				Archetype *targetArch{getOrCreateArchetype(regularMask)};

				auto [chunk, slot]{targetArch->addEntity(entity, copyData, moveData, tagMask)};

				assert(entity.index < mRecords.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				mRecords[entity.index] = {.generation = entity.generation,
										  .archetypeID = targetArch->getId(),
										  .chunkIndex = chunk,
										  .slotIndex = slot,
										  .state = State::Active};

				return entity;
			}

			// Component management
			/*! @brief Adds or updates a component of type `T` on `entity`.
				@tparam T Component type.
				@param[in] entity Target entity.
				@param[in] value Value to set (moved when possible).
				@note If the component type is a tag, the tag bit is set instead.
			*/
			template <typename T>
			void addComponent(Entity entity, T value)
			{
				if (!alive(entity))
				{
					return;
				}

				ComponentTypeID compID{componentID<T>()};

				assert(compID < ComponentInfos.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const ComponentInfo &info{ComponentInfos[compID]};

				if (info.isTag)
				{
					assert(entity.index < mRecords.size());

					// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					const EntityRecord &rec{mRecords[entity.index]};

					assert(rec.archetypeID < mArchetypePtrs.size());

					mArchetypePtrs[rec.archetypeID]->setTag(rec.chunkIndex, rec.slotIndex, compID);
					// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

					return;
				}

				assert(entity.index < mRecords.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				assert(mRecords[entity.index].archetypeID < mArchetypePtrs.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const Archetype *arch{mArchetypePtrs[mRecords[entity.index].archetypeID].get()};

				const ComponentMask oldRegular{arch->getRegularMask()};
				ComponentMask newRegular{oldRegular};

				newRegular.setBit(compID);

				if (oldRegular == newRegular)
				{
					T *ptr{static_cast<T *>(getComponentPtr(entity, compID))};
					*ptr = std::move(value);

					assert(entity.index < mRecords.size());

					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					assert(mRecords[entity.index].archetypeID < mArchetypePtrs.size());

					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					mArchetypePtrs[mRecords[entity.index].archetypeID]->bumpComponentVersion(mRecords[entity.index].chunkIndex, compID);

					return;
				}

				const std::array<const void *, MAX_COMPONENTS> copyData{};
				std::array<void *, MAX_COMPONENTS> moveData{};

				assert(compID < moveData.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				moveData[compID] = &value;

				moveEntity(entity, newRegular, copyData, moveData);
			}

			/*! @brief Removes component of type `T` from `entity`.
				@tparam T Component type to remove.
				@param[in] entity Target entity.
			*/
			template <typename T>
			void removeComponent(Entity entity)
			{
				if (!alive(entity))
				{
					return;
				}

				ComponentTypeID compID{componentID<T>()};

				assert(compID < ComponentInfos.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const ComponentInfo &info{ComponentInfos[compID]};

				if (info.isTag)
				{
					assert(entity.index < mRecords.size());

					// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					const EntityRecord &rec = mRecords[entity.index];

					assert(rec.archetypeID < mArchetypePtrs.size());

					mArchetypePtrs[rec.archetypeID]->clearTag(rec.chunkIndex, rec.slotIndex, compID);
					// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

					return;
				}

				removeComponent(entity, compID); // non‑template version already updated
			}

			/*! @brief Returns a pointer to component `T` for `entity`.
				@tparam T Component type.
				@param[in] entity Entity to query.
				@return Pointer to component data or nullptr if not present.
			*/
			template <typename T>
			T *getComponent(Entity entity)
			{
				return static_cast<T *>(getComponentPtr(entity, componentID<T>()));
			}

			/*! @brief Const version: returns const pointer to component `T` for `entity`.
				@tparam T Component type.
				@param[in] entity Entity to query.
				@return Const pointer to component data or nullptr if not present.
			*/
			template <typename T>
			const T *getComponent(Entity entity) const
			{
				return static_cast<const T *>(getComponentPtr(entity, componentID<T>()));
			}

			/*! @brief Adds a tag of type `Tag` to `entity`.
				@tparam Tag Tag type (must be registered as a tag component).
				@param[in] entity Target entity.
			*/
			template <typename Tag>
			void addTag(Entity entity)
			{
				if (!alive(entity))
				{
					return;
				}

				ComponentTypeID tagID{componentID<Tag>()};

				assert(tagID < ComponentInfos.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				if (!ComponentInfos[tagID].isTag)
				{
					return;
				}

				assert(entity.index < mRecords.size());

				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const EntityRecord &rec{mRecords[entity.index]};

				assert(rec.archetypeID < mArchetypePtrs.size());

				mArchetypePtrs[rec.archetypeID]->setTag(rec.chunkIndex, rec.slotIndex, tagID);
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			}

			/*! @brief Removes a tag of type `Tag` from `entity`.
				@tparam Tag Tag type to remove.
				@param[in] entity Target entity.
			*/
			template <typename Tag>
			void removeTag(Entity entity)
			{
				if (!alive(entity))
				{
					return;
				}

				ComponentTypeID tagID{componentID<Tag>()};

				assert(tagID < ComponentInfos.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				if (!ComponentInfos[tagID].isTag)
				{
					return;
				}

				assert(entity.index < mRecords.size());

				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const EntityRecord &rec = mRecords[entity.index];

				assert(rec.archetypeID < mArchetypePtrs.size());

				mArchetypePtrs[rec.archetypeID]->clearTag(rec.chunkIndex, rec.slotIndex, tagID);
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			}

			/*! @brief Tests whether `entity` has tag `Tag`.
				@tparam Tag Tag type to test.
				@param[in] entity Target entity.
				@return True if the tag is present, false otherwise.
			*/
			template <typename Tag>
			bool hasTag(Entity entity) const
			{
				if (!alive(entity))
				{
					return false;
				}

				ComponentTypeID tagID{componentID<Tag>()};

				assert(tagID < ComponentInfos.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				if (!ComponentInfos[tagID].isTag)
				{
					return false;
				}

				assert(entity.index < mRecords.size());

				// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const EntityRecord &rec = mRecords[entity.index];

				assert(rec.archetypeID < mArchetypePtrs.size());

				return mArchetypePtrs[rec.archetypeID]->hasTag(rec.chunkIndex, rec.slotIndex, tagID);
				// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			}

			/*! @brief Tests whether `entity` has component `T`.
				@tparam T Component type to test.
				@param[in] entity Target entity.
				@return True if the component is present, false otherwise.
			*/
			template <typename T>
			ATTR_NODISCARD bool hasComponent(Entity entity) const
			{
				return getComponent<T>(entity) != nullptr;
			}

			// Policy-based forEach implementations and dispatch helpers (included from separate header)
#include "ECS/forEachPolicy.h"

			// Query-filter forEach implementations and overloads (included from separate header)
#include "ECS/forEachQuery.h"

			// QueryBuilder, query<>() factory methods (nested class included from separate header)
#include "ECS/queryBuilder.h"

			// ViewIterator, View, view<>() factory methods (nested class included from separate header)
#include "ECS/ecsView.h"

		private:
			// MARK: Private Getters

			/*! @brief Returns a mutable pointer to the component storage for @p compID on @p entity.
				@param[in] entity The entity to query.
				@param[in] compID Component type identifier.
				@return Pointer to the component storage or nullptr if not present.
			*/
			void *getComponentPtr(const Entity &entity, const ComponentTypeID compID);

			/*! @brief Const overload of getComponentPtr.
				@param[in] entity The entity to query.
				@param[in] compID Component type identifier.
				@return Const pointer to the component storage or nullptr if not present.
			*/
			const void *getComponentPtr(const Entity &entity, const ComponentTypeID compID) const;

			/*! @brief Finds or creates an Archetype for @p regularMask.
				@param[in] regularMask ComponentMask describing the regular components for the archetype.
				@return Pointer to an existing or newly created `Archetype`.
			*/
			Archetype *getOrCreateArchetype(ComponentMask regularMask);

			// MARK: Private Member Functions

			/*! @brief Allocates an entity ID (reusing from the free list or creating a new one) without placing it in any archetype.
				@details This is a helper used by `createEntity()` and `createEntityWith()` to avoid the double archetype operation
			   of first placing an entity in the empty archetype and then immediately moving it.
				@return A new `Entity` handle with a valid index and generation. The entity is not yet in any archetype;
			   the caller must place it and populate `mRecords[entity.index]`.
			*/
			Entity allocateEntityID();

			/*! @brief Moves an entity to a new archetype, copying/moving component data as specified.
				@param[in] entity Entity to move.
				@param[in] newRegularMask Regular component mask for the destination archetype.
				@param[in] copyData Array of source pointers for components to copy.
				@param[in] moveData Array of source pointers for components to move.
				@param[in] newTags Optional tag mask to set on the destination chunk.
			*/
			void moveEntity(const Entity &entity, ComponentMask newRegularMask, const std::array<const void *, MAX_COMPONENTS> &copyData,
							const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask newTags = ComponentMask(0));

			/*! @brief Recursively destroys the hierarchy rooted at @p entity.
				@param[in] entity Root entity of the hierarchy to destroy.
			*/
			void destroyHierarchy(const Entity &entity);

			/*! @brief Compute override masks from provided copy/move arrays.
				@details Scans only the entries that are actually set (non-null) rather than iterating all MAX_COMPONENTS slots.
				@param[in] copyData Array of pointers to copy-source component values.
				@param[in] moveData Array of pointers to move-source component values.
				@param[out] moveOverrideMask Bitmask where move-specified components will be set.
				@param[out] copyOverrideMask Bitmask where copy-specified components will be set.
				@param[in] candidateMask Mask of component IDs that may have entries; only these are checked.
			*/
			static void acquireOverrideMasks(const std::array<const void *, MAX_COMPONENTS> &copyData,
											 const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask &moveOverrideMask,
											 ComponentMask &copyOverrideMask, const ComponentMask &candidateMask)
			{
				forEachSetBit(candidateMask, [&](const ComponentTypeID componentTypeID) {
					assert(componentTypeID < moveData.size());
					assert(componentTypeID < copyData.size());

					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					if (moveData[componentTypeID] != nullptr)
					{
						updateTagMask(componentTypeID, moveOverrideMask);
					}
					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					else if (copyData[componentTypeID] != nullptr)
					{
						updateTagMask(componentTypeID, copyOverrideMask);
					}
				});
			}

			/*! @brief Sets the bit for @p componentTypeID in @p mask (handles low/high bit partition).
				@param[in] componentTypeID Component type identifier.
				@param[in,out] mask ComponentMask to update.
			*/
			static void updateTagMask(const ComponentTypeID componentTypeID, ComponentMask &mask)
			{
				mask.setBit(componentTypeID);
			}

			// MARK: Private Template Member Functions

			/*! @brief Helper invoked during entity creation to classify a provided argument as a copy/move or tag.
				@tparam Param The original parameter type (for constness detection).
				@tparam Stored The decayed stored type.
				@param[in] componentTypeID Component type identifier for the argument.
				@param[out] copyData Array to record addresses for const (copy) parameters.
				@param[out] moveData Array to record addresses for movable parameters.
				@param[out] regularMask Updated to include this component if it is regular.
				@param[out] tagMask Updated to include this component if it is a tag.
				@param[in] stored The stored value (lvalue reference into the temporary storage tuple).
			*/
			template <typename Param, typename Stored>
			static void processCreateComponent(const ComponentTypeID componentTypeID, std::array<const void *, MAX_COMPONENTS> &copyData,
											   std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask &regularMask,
											   ComponentMask &tagMask, Stored &stored)
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				const ComponentInfo &info{ComponentInfos[componentTypeID]};

				if (info.isTag)
				{
					updateTagMask(componentTypeID, tagMask);
				}
				else
				{
					updateTagMask(componentTypeID, regularMask);

					if constexpr (std::is_const_v<std::remove_reference_t<Param>>)
					{
						assert(componentTypeID < copyData.size());

						// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
						copyData[componentTypeID] = &stored;
					}
					else
					{
						assert(componentTypeID < copyData.size());

						// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
						moveData[componentTypeID] = &stored;
					}
				}
			}

			/*! @brief Returns the cached list of archetypes matching a multi-clause query, computing and caching it if absent.
				@tparam Self The ECS type used for dispatch; may be const-qualified when called from const contexts.
				@tparam AnyList TypeList of alternative types used for the `Any<...>` clause (may be empty).
				@tparam NoneList TypeList of types used for the `None<...>` clause (may be empty).
				@param[in,out] self Reference to the ECS instance that owns the archetype list and query cache.
				@param[in] key QueryKey computed for the requested clause combination and used as the cache key.
				@param[in] requiredMask Bitmask of required (All) regular component types.
				@param[in] anyMask Bitmask representing the union of types in the Any clause (may be zero).
				@param[in] noneMask Bitmask representing the union of types in the None clause (may be zero).
				@return Reference to a std::vector of matching `Archetype *` stored inside the ECS instance's cache.
				@note If the key is not present in the cache the function computes the matching archetypes by iterating all archetypes and
			   testing the masks. The function uses compile-time checks on `AnyList`/`NoneList` to avoid unnecessary runtime tests when
			   clauses are empty.
			*/
			template <class Self, typename AnyList, typename NoneList>
			static const std::vector<Archetype *> &getMatchingArchetypesForQueryCalls(Self &self, const QueryKey &key,
																					  const ComponentMask &requiredMask,
																					  const ComponentMask &anyMask,
																					  const ComponentMask &noneMask)
			{
				// Fast path: check under shared lock
				{
					const std::shared_lock readLock(self.mMultiQueryMutex);
					auto iterator{self.mMultiQueryCache.find(key)};

					if (iterator != self.mMultiQueryCache.end())
					{
						return iterator->second;
					}
				}

				// Slow path: compute and insert under exclusive lock
				const std::unique_lock writeLock(self.mMultiQueryMutex);

				// Double-check after acquiring exclusive lock
				auto iterator{self.mMultiQueryCache.find(key)};

				if (iterator != self.mMultiQueryCache.end())
				{
					return iterator->second;
				}

				// Not cached – compute matching archetypes
				std::vector<Archetype *> matching;
				for (const auto &archPtr : self.mArchetypePtrs)
				{
					const ComponentMask archMask{archPtr->getRegularMask()};

					if ((archMask & requiredMask) != requiredMask)
					{
						continue;
					}

					if constexpr (TypeListSize<AnyList>::value != 0)
					{
						if (!(archMask & anyMask))
						{
							continue;
						}
					}

					if constexpr (TypeListSize<NoneList>::value != 0)
					{
						if (archMask & noneMask)
						{
							continue;
						}
					}

					matching.push_back(archPtr.get());
				}

				iterator = self.mMultiQueryCache.emplace(key, std::move(matching)).first;

				return iterator->second;
			}

			/*! @brief Collects archetype chunks whose versions indicate they need processing for the supplied mask.
				@param[out] dirtyChunks Vector to which matching (archetype, chunkIndex, ChunkVersion const*) tuples are appended.
				@param[in] matchingArchetypes Pre-filtered list of archetypes that match the query masks.
				@param[in] version SystemVersion used to decide whether a chunk requires an update for the requested mask.
				@param[in] requiredRegular ComponentMask of the regular components that the system depends on; used when querying
			   `version.needsUpdate` so only relevant component changes mark a chunk dirty.
				@note Empty chunks (no entities) are skipped. The function appends pointers into the archetype-managed chunk versions;
			   callers must ensure archetypes outlive use of the returned ChunkVersion pointers.
			*/
			static void setDirtyChunks(std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> &dirtyChunks,
									   const std::vector<Archetype *> &matchingArchetypes, const SystemVersion &version,
									   const ComponentMask &requiredRegular, const ComponentMask &changedMask = ComponentMask(0, 0))
			{
				for (Archetype *arch : matchingArchetypes)
				{
					ui chunkCount{arch->getChunkCount()};

					for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
					{
						if (arch->getEntityCount(chunkIndex) == 0)
						{
							continue;
						}

						const ChunkVersion &chunkVersion{arch->getChunkVersion(chunkIndex)};

						if (changedMask)
						{
							// Changed<T> filter: include chunk if the global chunk version is newer
							// (newly created/moved entities) OR if at least one of the specified
							// Changed component versions has advanced since the system last ran.
							bool hasChange{false};

							if (chunkVersion.getVersion() > version.getVersion())
							{
								hasChange = true;
							}
							else
							{
								forEachSetBit(changedMask, [&](const ComponentTypeID compID) {
									if (chunkVersion.getComponentVersion(compID) > version.getComponentVersion(compID))
									{
										hasChange = true;
									}
								});
							}

							if (hasChange)
							{
								dirtyChunks.emplace_back(arch, chunkIndex, &chunkVersion);
							}
						}
						else if (version.needsUpdate(chunkVersion, requiredRegular))
						{
							dirtyChunks.emplace_back(arch, chunkIndex, &chunkVersion);
						}
					}
				}
			}

			/*! @brief Bulk-merge version information from processed dirty chunks into a `SystemVersion`.
				@details Performs a single pass over all dirty chunks to compute the maximum global and per-component versions,
			   then applies the result to @p version. This replaces per-chunk `forEachSetBit` + lambda calls with a more
			   cache-friendly linear scan.
				@param[in,out] version SystemVersion to update with the maximum observed versions.
				@param[in] dirtyChunks The chunks that were processed; their `ChunkVersion` pointers are read.
				@param[in] requiredMask Mask of component IDs whose per-component versions should be merged.
			*/
			static void bulkMergeVersions(SystemVersion &version,
										  const std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> &dirtyChunks,
										  const ComponentMask &requiredMask)
			{
				VersionType maxGlobalVersion{version.getVersion()};

				for (const auto &[arch, chunkIndex, chunkVer] : dirtyChunks)
				{
					maxGlobalVersion = std::max(maxGlobalVersion, arch->getChunkVersion(chunkIndex).getVersion());
				}

				version.setVersion(maxGlobalVersion);

				forEachSetBit(requiredMask, [&](const ComponentTypeID compID) {
					VersionType maxVer{version.getComponentVersion(compID)};

					for (const auto &[arch, chunkIndex, chunkVer] : dirtyChunks)
					{
						maxVer = std::max(maxVer, chunkVer->getComponentVersion(compID));
					}

					version.setComponentVersion(compID, maxVer);
				});
			}

			friend class CommandBuffer;

			/*! @struct IterationGuard include/ECS/ecs.h
				@brief RAII guard that increments/decrements the active-iteration counter.
				@details Used by `forEach` dispatch functions to track whether any iteration is in progress.
			   Structural mutators (e.g. `getOrCreateArchetype`) assert the counter is zero to detect
			   concurrent structural changes during iteration.
			*/
			class IterationGuard
			{
				public:
					// Do not allow moves for this class
					IterationGuard(IterationGuard &&) = delete ("IterationGuard is not movable");
					IterationGuard &operator=(IterationGuard &&) = delete ("IterationGuard is not movable");

					explicit IterationGuard(std::atomic<int32_t> &counter) noexcept : mCounter(counter)
					{
						mCounter.fetch_add(1, std::memory_order_acquire);
					}

					~IterationGuard() noexcept
					{
						mCounter.fetch_sub(1, std::memory_order_release);
					}

					IterationGuard(const IterationGuard &) = delete;
					IterationGuard &operator=(const IterationGuard &) = delete;

				private:
					std::atomic<int32_t> &mCounter;
			};

		private:
			/*! @var mActiveIterations
				@brief Atomic counter tracking the number of active `forEach` iterations.
				@details Incremented by `IterationGuard` when a `forEach` dispatch begins and decremented
			   when it completes. Structural mutators assert this is zero to detect data races.
			*/
			mutable std::atomic<int32_t> mActiveIterations{0};

			/*! @var mQueryCache
				@brief Cache mapping required component masks to matching archetype lists for fast query resolution.
			*/
			QueryCache mQueryCache{};

			mutable std::unordered_map<QueryKey, std::vector<Archetype *>> mMultiQueryCache;

			/*! @var mMultiQueryMutex
				@brief Shared mutex protecting `mMultiQueryCache` for concurrent reads and exclusive writes.
			*/
			mutable std::shared_mutex mMultiQueryMutex;

			/*! @var mThreadPool
				@brief Mutable thread pool used for parallel `forEach`/system execution where threads are needed.
			*/
			mutable ThreadPool mThreadPool{};

			/*! @var mWorkStealingPool
				@brief Mutable work-stealing pool for dynamic load balancing across chunks.
			*/
			mutable WorkStealingPool mWorkStealingPool{};

			/*! @var mHierarchyMutex
				@brief Protects parent/child hierarchy mutations.
			*/
			mutable std::mutex mHierarchyMutex{};

			/*! @var mArchetypeMaskToID
				@brief Map from component masks to archetype identifiers for quick lookup.
			*/
			std::unordered_map<ComponentMask, ui> mArchetypeMaskToID{};

			/*! @var mRecords
				@brief Per-entity records containing archetype, chunk and slot indices.
			*/
			std::vector<EntityRecord> mRecords;

			/*! @var mFreeIndices
				@brief Free-list of entity indices available for reuse.
			*/
			std::vector<ui> mFreeIndices;

			/*! @var mArchetypePtrs
				@brief Owned pointers to all archetype instances managed by the ECS.
			*/
			std::vector<std::unique_ptr<Archetype>> mArchetypePtrs;

			/*! @var mParent
				@brief Parallel vector indexed by entity index storing each entity's parent handle (or invalid entity).
			*/
			std::vector<Entity> mParent;

			/*! @var mChildren
				@brief Parallel vector indexed by entity index storing a vector of direct children for that entity.
			*/
			std::vector<std::vector<Entity>> mChildren;

			/*! @var mNextEntityIndex
				@brief Monotonically increasing counter used to assign new entity indices when free-list is empty.
			*/
			ui mNextEntityIndex{0};
	};
} // namespace Dimensia::ECS

#endif