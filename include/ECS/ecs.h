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
#include <latch>
#include <memory>
#include <mutex>
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

				Entity entity{createEntity()};

				if (regularMask || tagMask)
				{
					if (regularMask)
					{
						moveEntity(entity, regularMask, copyData, moveData, tagMask);
					}
					else
					{
						moveEntity(entity, ComponentMask(0), copyData, moveData, tagMask);
					}
				}

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

				if (compID < LOWER_HALF_BIT_MASK)
				{
					newRegular.mLow |= (1U << compID);
				}
				else
				{
					newRegular.mHigh |= (1U << (compID - LOWER_HALF_BIT_MASK));
				}

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

			// MARK: forEach Template Member Functions

			/*! @brief Convenience overload that iterates entities matching `Components...` using the sequential execution policy.
				@tparam Components Component types to include in the query.
				@tparam Func Callable type. The callable is forwarded to the underlying implementation and may have one of the supported
			   signatures used by the `forEach` helpers (per-entity or per-chunk forms). See `processChunkEntities` helpers for exact
			   expected callable shapes.
				@param[in] func User-provided callable that will be invoked for matching entities or chunks.
				@note This overload simply forwards to `forEach<Components...>(ExecutionPolicy::Seq, std::forward<Func>(func))`.
			*/
			template <typename... Components, typename Func>
			void forEach(Func &&func)
			{
				forEach<Components...>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			/*! @brief Const overload of the sequential `forEach` convenience wrapper.
				@tparam Components Component types to include in the query.
				@tparam Func Callable type; callable must be compatible with the const processing helpers (it will not modify ECS
			   internals).
				@param[in] func User-provided callable forwarded to the underlying const implementation.
				@note Use this overload when only read access to components/entities is required.
			*/
			template <typename... Components, typename Func>
			void forEach(Func &&func) const
			{
				forEach<Components...>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			// MARK: forEachPolicyImpl

			/*! @brief Sequential chunk processing implementation used by `forEach`.
				@tparam Func Callable compatible with `(Archetype*, ui, ui)`.
				@param[in] matchingArchetypes Archetypes matching the query.
				@param[in] processChunk Callable invoked for each non-empty chunk with `(arch, chunkIndex, entityCount)`.
			*/
			template <typename Func>
				requires InvocableWithArgs<Func, Archetype *, ui, ui>
			static void forEachPolicySeqImpl(const std::vector<Archetype *> &matchingArchetypes, Func &&processChunk)
			{
				Func processChunkFunction{std::forward<Func>(processChunk)};

				for (Archetype *arch : matchingArchetypes)
				{
					const ui chunkCount{arch->getChunkCount()};

					for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
					{
						const ui entityCount{arch->getEntityCount(chunkIndex)};

						if (entityCount == 0)
						{
							continue;
						}

						processChunkFunction(arch, chunkIndex, entityCount);
					}
				}
			}

			/*! @brief Parallel per-chunk processing using `ThreadPool`.
				@tparam Func Callable compatible with `(Archetype*, ui, ui)`.
				@param[in] matchingArchetypes Archetypes matching the query.
				@param[in,out] threadPool Thread pool used to submit per-chunk tasks.
				@param[in] processChunk Callable invoked per-chunk; tasks are waited on before return.
			*/
			template <typename Func>
				requires InvocableWithArgs<Func, Archetype *, ui, ui>
			static void forEachPolicyParImpl(const std::vector<Archetype *> &matchingArchetypes, ThreadPool &threadPool,
											 Func &&processChunk)
			{
				std::vector<std::future<void>> futures;
				futures.reserve(matchingArchetypes.size() * 2);

				Func processChunkFunction{std::forward<Func>(processChunk)};

				for (Archetype *arch : matchingArchetypes)
				{
					ui chunkCount{arch->getChunkCount()};

					for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
					{
						ui entityCount{arch->getEntityCount(chunkIndex)};

						if (entityCount == 0)
						{
							continue;
						}

						futures.push_back(threadPool.submit(processChunkFunction, arch, chunkIndex, entityCount));
					}
				}

				for (std::future<void> &fut : futures)
				{
					fut.get();
				}
			}

			/*! @brief Parallel batched processing: groups chunks into batches and schedules each batch.
				@tparam Components Component types used by per-entity helpers.
				@tparam Func Callable invoked per-entity/ per-chunk.
				@param[in] matchingArchetypes Archetypes matching the query.
				@param[in,out] threadPool Thread pool used to execute batches.
				@param[in] func User callable forwarded into batch tasks.
			*/
			template <typename... Components, typename Func>
			static void forEachPolicyParBatchedImpl(const std::vector<Archetype *> &matchingArchetypes, ThreadPool &threadPool, Func &&func)
			{
				std::vector<std::pair<Archetype *, ui>> allChunks;

				for (Archetype *arch : matchingArchetypes)
				{
					const ui chunkCount{arch->getChunkCount()};
					for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
					{
						if (arch->getEntityCount(chunkIndex) > 0)
						{
							allChunks.emplace_back(arch, chunkIndex);
						}
					}
				}

				if (allChunks.empty())
				{
					return;
				}

				const std::size_t batchSize{getBatchSize(allChunks.size())};

				std::latch latch(static_cast<std::ptrdiff_t>((allChunks.size() + batchSize - 1) / batchSize));

				// Move the user-provided callable into a shared pointer once so it can be safely
				// captured by each batch task without forwarding/moving `func` multiple times.
				using FuncT = std::decay_t<Func>;
				FuncT sharedFunc = std::forward<Func>(func);

				for (std::size_t i{0}; i < allChunks.size(); i += batchSize)
				{
					const std::size_t end{std::min(i + batchSize, allChunks.size())};
					const std::vector<std::pair<Archetype *, ui>> batch(allChunks.begin() + static_cast<std::ptrdiff_t>(i),
																		allChunks.begin() + static_cast<std::ptrdiff_t>(end));

					threadPool.submitWithLatch(
						[batch, sharedFunc]() mutable {
							for (const auto &[arch, chunkIndex] : batch)
							{
								ui entityCount{arch->getEntityCount(chunkIndex)};
								Entity *entityArr = arch->getEntityArray(chunkIndex);
								processChunkEntities<Components...>(entityArr, entityCount, chunkIndex, arch, sharedFunc);
							}
						},
						latch);
				}

				latch.wait();
			}

			/*! @brief Parallel processing using a work-stealing pool for dynamic load balancing.
				@tparam Components Component types used by per-entity helpers.
				@tparam Func Callable invoked per-entity/ per-chunk.
				@param[in] matchingArchetypes Archetypes matching the query.
				@param[in,out] workStealingPool Work-stealing pool used to execute chunk processing.
				@param[in] func User callable forwarded into worker tasks.
			*/
			template <typename... Components, typename Func>
			static void forEachPolicyParStealingImpl(const std::vector<Archetype *> &matchingArchetypes, WorkStealingPool &workStealingPool,
													 Func &&func)
			{
				std::vector<std::pair<Archetype *, ui>> allChunks;

				for (Archetype *arch : matchingArchetypes)
				{
					ui chunkCount{arch->getChunkCount()};
					for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
					{
						if (arch->getEntityCount(chunkIndex) > 0)
						{
							allChunks.emplace_back(arch, chunkIndex);
						}
					}
				}

				if (allChunks.empty())
				{
					return;
				}

				const std::size_t batchSize{getBatchSize(allChunks.size())};

				std::latch latch(static_cast<std::ptrdiff_t>((allChunks.size() + batchSize - 1) / batchSize));

				using FuncT = std::decay_t<Func>;
				const FuncT processChunkFunction{std::forward<Func>(func)};

				workStealingPool.submitChunks(
					allChunks,
					[processChunkFunction](Archetype *arch, ui chunkIndex) {
						ui entityCount{arch->getEntityCount(chunkIndex)};
						Entity *entityArr = arch->getEntityArray(chunkIndex);
						processChunkEntities<Components...>(entityArr, entityCount, chunkIndex, arch, processChunkFunction);
					},
					latch, batchSize);

				latch.wait();
			}

			/*! @brief Policy dispatcher for `forEach` that selects the execution strategy.
				@tparam Self The ECS type (possibly const-qualified) for method dispatch.
				@tparam Components Component types for the query.
				@tparam Func User-provided callable.
				@param[in,out] self Reference to ECS instance for accessing caches and pools.
				@param[in] policy Execution policy to use.
				@param[in] func Callable to execute for matching entities or chunks.
			*/
			template <class Self, typename... Components, typename Func>
			static void forEachPolicyImpl(Self &self, const ExecutionPolicy &policy, Func &&func)
			{
				constexpr ComponentMask requiredRegular{buildRequiredMask<Components...>()};
				const std::vector<Archetype *> &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

				auto processChunk = [&](Archetype *arch, const ui chunkIndex, const ui entityCount) {
					if constexpr (std::is_const_v<Self>)
					{
						const Entity *entities{arch->getEntityArray(chunkIndex)};
						processChunkEntitiesConst<Components...>(entities, entityCount, chunkIndex, arch, std::forward<Func>(func));
					}
					else
					{
						Entity *entities{arch->getEntityArray(chunkIndex)};
						processChunkEntities<Components...>(entities, entityCount, chunkIndex, arch, std::forward<Func>(func));
					}
				};

				switch (policy)
				{
					case ExecutionPolicy::Seq:
						forEachPolicySeqImpl(matchingArchetypes, processChunk);
						break;
					case ExecutionPolicy::Par:
						forEachPolicyParImpl(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParBatched:
						forEachPolicyParBatchedImpl<Components...>(matchingArchetypes, self.mThreadPool, std::forward<Func>(func));
						break;
					case ExecutionPolicy::ParStealing:
						forEachPolicyParStealingImpl<Components...>(matchingArchetypes, self.mWorkStealingPool, std::forward<Func>(func));
						break;
					default:
						assert(false && "Invalid execution policy");
				}
			}

			/*! @brief Iterate over entities matching `Components...` using the specified execution policy.
				@tparam Components Component types to include in the query.
				@tparam Func Callable type provided by the user. The callable will be forwarded to the policy implementation and must match
			   one of the callable signatures supported by the processing helpers (per-chunk or per-entity forms).
				@param[in] policy Execution policy that controls parallelism and batching (Seq, Par, ParBatched, ParStealing).
				@param[in] func User-provided callable invoked for matching entities or chunks.
				@note This forwards to `forEachPolicyImpl` which performs dispatch based on @p policy.
			*/
			template <typename... Components, typename Func>
			void forEach(const ExecutionPolicy &policy, Func &&func)

			{
				forEachPolicyImpl<decltype(*this), Components...>(*this, policy, std::forward<Func>(func));
			}

			/*! @brief Const overload: iterate over entities matching `Components...` using @p policy without mutating ECS state.
				@tparam Components Component types to include in the query.
				@tparam Func Callable type; must be compatible with the const processing helpers.
				@param[in] policy Execution policy to use.
				@param[in] func User-provided callable forwarded to the const policy implementation.
				@note Use this overload for read-only systems to enable safe parallel execution where applicable.
			*/
			template <typename... Components, typename Func>
			void forEach(const ExecutionPolicy &policy, Func &&func) const
			{
				forEachPolicyImpl<decltype(*this), Components...>(*this, policy, std::forward<Func>(func));
			}

			// MARK: forEachPolicyCommandImpl

			/*! @brief Sequential command-style chunk processing used when user functions accept an entity and its components.
				@tparam Func Callable invoked per (arch, chunkIndex).
				@param[in] matchingArchetypes Archetypes matching the query.
				@param[in] processChunk Callable invoked per chunk.
			*/
			template <typename Func>
			static void forEachPolicyCommandSeqImpl(const std::vector<Archetype *> &matchingArchetypes, Func &&processChunk)
			{
				const Func processChunkFunction{std::forward<Func>(processChunk)};

				for (Archetype *arch : matchingArchetypes)
				{
					const ui chunkCount{arch->getChunkCount()};

					for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
					{
						if (arch->getEntityCount(chunkIndex) == 0)
						{
							continue;
						}

						processChunkFunction(arch, chunkIndex);
					}
				}
			}

			/*! @brief Parallel command-style per-chunk processing using a `ThreadPool`.
				@tparam Func Callable invoked per chunk `(arch, chunkIndex)`.
				@param[in] matchingArchetypes Archetypes matching the query.
				@param[in,out] threadPool Thread pool used to execute tasks.
				@param[in] processChunk Callable invoked per chunk.
			*/
			template <typename Func>
			static void forEachPolicyCommandParImpl(const std::vector<Archetype *> &matchingArchetypes, ThreadPool &threadPool,
													Func &&processChunk)
			{
				std::vector<std::future<void>> futures;
				futures.reserve(matchingArchetypes.size() * 2);

				for (Archetype *arch : matchingArchetypes)
				{
					const ui chunkCount{arch->getChunkCount()};

					for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
					{
						if (arch->getEntityCount(chunkIndex) == 0)
						{
							continue;
						}

						futures.push_back(threadPool.submit(
							[arch, chunkIndex, &function = std::forward<Func>(processChunk)]() { function(arch, chunkIndex); }));
					}
				}

				for (std::future<void> &fut : futures)
				{
					fut.get();
				}
			}

			/*! @brief Parallel batched command-style processing.
				@tparam Func Callable invoked per chunk.
				@param[in] matchingArchetypes Archetypes matching the query.
				@param[in,out] threadPool Thread pool used to execute batched tasks.
				@param[in] processChunk Callable invoked per chunk.
			*/
			template <typename Func>
			static void forEachPolicyCommandParBatchedImpl(const std::vector<Archetype *> &matchingArchetypes, ThreadPool &threadPool,
														   Func &&processChunk)
			{
				std::vector<std::pair<Archetype *, ui>> allChunks;

				for (Archetype *arch : matchingArchetypes)
				{
					const ui chunkCount{arch->getChunkCount()};

					for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
					{
						if (arch->getEntityCount(chunkIndex) > 0)
						{
							allChunks.emplace_back(arch, chunkIndex);
						}
					}
				}

				if (allChunks.empty())
				{
					return;
				}

				const std::size_t batchSize{getBatchSize(allChunks.size())};

				std::latch latch(static_cast<std::ptrdiff_t>((allChunks.size() + batchSize - 1) / batchSize));

				for (std::size_t i{0}; i < allChunks.size(); i += batchSize)
				{
					const std::size_t end{std::min(i + batchSize, allChunks.size())};
					const std::vector<std::pair<Archetype *, ui>> batch(allChunks.begin() + static_cast<std::ptrdiff_t>(i),
																		allChunks.begin() + static_cast<std::ptrdiff_t>(end));

					threadPool.submitWithLatch(
						[batch, &function = std::forward<Func>(processChunk)]() {
							for (const auto &[arch, chunkIndex] : batch)
							{
								function(arch, chunkIndex);
							}
						},
						latch);
				}

				latch.wait();
			}

			/*! @brief Parallel command-style processing using a work-stealing pool.
				@tparam Func Callable invoked per chunk.
				@param[in] matchingArchetypes Archetypes matching the query.
				@param[in,out] workStealingPool Work-stealing pool used to execute chunk tasks.
				@param[in] processChunk Callable invoked per chunk.
			*/
			template <typename Func>
			static void forEachPolicyCommandParStealingImpl(const std::vector<Archetype *> &matchingArchetypes,
															WorkStealingPool &workStealingPool, Func &&processChunk)
			{
				std::vector<std::pair<Archetype *, ui>> allChunks;

				for (Archetype *arch : matchingArchetypes)
				{
					const ui chunkCount{arch->getChunkCount()};

					for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
					{
						if (arch->getEntityCount(chunkIndex) > 0)
						{
							allChunks.emplace_back(arch, chunkIndex);
						}
					}
				}

				if (allChunks.empty())
				{
					return;
				}

				const std::size_t batchSize{getBatchSize(allChunks.size())};
				std::latch latch(static_cast<std::ptrdiff_t>((allChunks.size() + batchSize - 1) / batchSize));

				workStealingPool.submitChunks(
					allChunks,
					[&function = std::forward<Func>(processChunk)](Archetype *arch, ui chunkIndex) { function(arch, chunkIndex); }, latch,
					batchSize);

				latch.wait();
			}

			/*! @brief Dispatches command-style processing according to @p policy.
				@tparam Self The ECS type (possibly const-qualified).
				@tparam Components Component types in the query.
				@tparam Func Callable accepting `(Entity, Components...)` or similar along with optional CommandBuffer.
				@param[in,out] self ECS instance reference.
				@param[in] policy Execution policy to use.
				@param[in] cmds CommandBuffer passed to callbacks when required.
				@param[in] func User callable invoked for each entity or chunk.
			*/
			template <class Self, typename... Components, typename Func>
			static void forEachPolicyCommandImpl(Self &self, const ExecutionPolicy &policy, CommandBuffer & /*cmds*/, Func &&func)
			{
				constexpr ComponentMask requiredRegular{buildRequiredMask<Components...>()};
				const std::vector<Archetype *> &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

				// Processing lambda – uses the appropriate chunk function based on constness
				auto processChunk = [&, function = std::forward<Func>(func)](Archetype *arch, ui chunkIndex) {
					const ui entityCount{arch->getEntityCount(chunkIndex)};

					if constexpr (std::is_const_v<Self>)
					{
						const Entity *entityArr = arch->getEntityArray(chunkIndex);
						processChunkEntitiesConst<Components...>(entityArr, entityCount, chunkIndex, arch,
																 [&](Entity entity, const auto &...comps) { function(entity, comps...); });
					}
					else
					{
						Entity *entityArr = arch->getEntityArray(chunkIndex);
						processChunkEntities<Components...>(entityArr, entityCount, chunkIndex, arch,
															[&](Entity entity, auto &...comps) { function(entity, comps...); });
					}
				};

				switch (policy)
				{
					case ExecutionPolicy::Seq:
						forEachPolicyCommandSeqImpl(matchingArchetypes, processChunk);
						break;
					case ExecutionPolicy::Par:

						forEachPolicyCommandParImpl(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParBatched:
						forEachPolicyCommandParBatchedImpl(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParStealing:
						forEachPolicyCommandParStealingImpl(matchingArchetypes, self.mWorkStealingPool, processChunk);
						break;
					default:
						assert(false && "Invalid execution policy");
				}
			}

			/*! @brief Iterate over matching entities with a `CommandBuffer` provided to callbacks using @p policy.
				@tparam Components Component types to query.
				@tparam Func Callable invoked with signature `(Entity, Components..., CommandBuffer&)` or similar.
				@param[in] policy Execution policy to use.
				@param[in,out] cmds CommandBuffer forwarded to user callbacks.
				@param[in] func User callable.
			*/
			template <typename... Components, typename Func>
			void forEach(const ExecutionPolicy &policy, CommandBuffer &cmds, Func &&func)
			{
				forEachPolicyCommandImpl<decltype(*this), Components...>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Const overload of the `forEach` variant that provides a `CommandBuffer` to callbacks.
			 */
			template <typename... Components, typename Func>
			void forEach(const ExecutionPolicy &policy, CommandBuffer &cmds, Func &&func) const
			{
				forEachPolicyCommandImpl<decltype(*this), Components...>(*this, policy, cmds, std::forward<Func>(func));
			}

			// MARK: forEachPolicyVersionImpl

			/*! @brief Sequential processing for dirty chunks with post-processing version updates.
				@tparam Func Callable invoked per chunk `(Archetype*, ui)`.
				@tparam Ver Callable used to update version information after processing each chunk.
				@param[in] dirtyChunks Vector of tuples `(arch, chunkIndex, chunkVersionPtr)` indicating work.
				@param[in] processFunc Callable executed on each dirty chunk.
				@param[in] updateVersion Callable used to update system/component versions after processing.
			*/
			template <typename Func, typename Ver>
			static void forEachPolicyVersionAndVersionCommandSeqImpl(
				const std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> &dirtyChunks, Func &&processFunc, Ver &&updateVersion)
			{
				const Func processChunkFunction{std::forward<Func>(processFunc)};
				const Ver updateVersionFunction{std::forward<Ver>(updateVersion)};

				for (const auto &chunk : dirtyChunks)
				{
					processChunkFunction(std::get<0>(chunk), std::get<1>(chunk));
					updateVersionFunction(chunk);
				}
			}

			/*! @brief Parallel processing of dirty chunks using ThreadPool; updates versions after processing.
				@tparam Func Callable invoked per chunk.
				@tparam Ver Callable to update version metadata.
				@param[in] dirtyChunks Chunks requiring processing.
				@param[in,out] threadPool ThreadPool used for parallel execution.
				@param[in] processFunc Callable executed in parallel for each dirty chunk.
				@param[in] updateVersion Callable executed once per chunk after processing to update versions.
			*/
			template <typename Func, typename Ver>
			static void forEachPolicyVersionandVersionCommandParImpl(
				const std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> &dirtyChunks, ThreadPool &threadPool,
				Func &&processFunc, Ver &&updateVersion)
			{
				std::vector<std::future<void>> futures;
				futures.reserve(dirtyChunks.size());

				const Func processChunkFunction{std::forward<Func>(processFunc)};
				const Ver updateVersionFunction{std::forward<Ver>(updateVersion)};

				for (const auto &chunk : dirtyChunks)
				{
					Archetype *arch{std::get<0>(chunk)};
					ui chunkIndex{std::get<1>(chunk)};

					futures.push_back(
						threadPool.submit([arch, chunkIndex, processChunkFunction]() { processChunkFunction(arch, chunkIndex); }));
				}

				for (std::future<void> &fut : futures)
				{
					fut.get();
				}

				for (const auto &chunk : dirtyChunks)
				{
					updateVersionFunction(chunk);
				}
			}

			/*! @brief Parallel batched processing for dirty chunks with post-update of versions.
				@tparam Func Callable invoked per chunk.
				@tparam Ver Callable used to update version info after processing.
				@param[in] dirtyChunks Chunks requiring processing.
				@param[in,out] threadPool ThreadPool used for batched submission.
				@param[in] processFunc Callable executed inside worker tasks.
				@param[in] updateVersion Callable applied after processing to adjust system versions.
			*/
			template <typename Func, typename Ver>
			static void forEachPolicyVersionandVersionCommandParBatchedImpl(
				const std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> &dirtyChunks, ThreadPool &threadPool,
				Func &&processFunc, Ver &&updateVersion)
			{
				// Convert to simple pairs for batching
				std::vector<std::pair<Archetype *, ui>> chunks;
				chunks.reserve(dirtyChunks.size());

				const Func processChunkFunction{std::forward<Func>(processFunc)};
				const Ver updateVersionFunction{std::forward<Ver>(updateVersion)};

				for (const auto &chunk : dirtyChunks)
				{
					chunks.emplace_back(std::get<0>(chunk), std::get<1>(chunk));
				}

				const std::size_t batchSize{getBatchSize(chunks.size())};

				std::latch latch(static_cast<std::ptrdiff_t>((chunks.size() + batchSize - 1) / batchSize));

				for (std::size_t i{0}; i < chunks.size(); i += batchSize)
				{
					const std::size_t end{std::min(i + batchSize, chunks.size())};
					const std::vector<std::pair<Archetype *, ui>> batch(chunks.begin() + static_cast<std::ptrdiff_t>(i),
																		chunks.begin() + static_cast<std::ptrdiff_t>(end));

					threadPool.submitWithLatch(
						[batch, processChunkFunction]() {
							for (const auto &[arch, chunkIndex] : batch)
							{
								processChunkFunction(arch, chunkIndex);
							}
						},
						latch);
				}

				latch.wait();

				for (const auto &chunk : dirtyChunks)
				{
					updateVersionFunction(chunk);
				}
			}

			/*! @brief Parallel dirty-chunk processing using work-stealing; updates versions after processing.
				@tparam Func Callable invoked per chunk.
				@tparam Ver Callable used to update version info after processing.
				@param[in] dirtyChunks Chunks requiring processing.
				@param[in,out] workStealingPool Work-stealing pool used to execute batch tasks.
				@param[in] processFunc Callable executed per chunk.
				@param[in] updateVersion Callable applied after processing to adjust system versions.
			*/
			template <typename Func, typename Ver>
			static void forEachPolicyVersionandVersionCommandParStealingImpl(
				const std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> &dirtyChunks, WorkStealingPool &workStealingPool,
				Func &&processFunc, Ver &&updateVersion)
			{
				std::vector<std::pair<Archetype *, ui>> chunks;
				chunks.reserve(dirtyChunks.size());

				const Func processChunkFunction{std::forward<Func>(processFunc)};
				const Ver updateVersionFunction{std::forward<Ver>(updateVersion)};

				for (const auto &chunk : dirtyChunks)
				{
					chunks.emplace_back(std::get<0>(chunk), std::get<1>(chunk));
				}

				const std::size_t batchSize{getBatchSize(chunks.size())}; // same as in forEachPolicyImpl
				std::latch latch(static_cast<std::ptrdiff_t>((chunks.size() + batchSize - 1) / batchSize));

				workStealingPool.submitChunks(
					chunks, [processChunkFunction](Archetype *arch, ui chunkIndex) { processChunkFunction(arch, chunkIndex); }, latch,
					batchSize);

				latch.wait();

				for (const auto &chunk : dirtyChunks)
				{
					updateVersionFunction(chunk);
				}
			}

			/*! @brief Dispatches processing of chunks that are dirty with respect to @p version.
				@tparam Self The ECS type (possibly const-qualified).
				@tparam Components Component types used to build the required mask.
				@tparam Func User callable invoked per-entity or per-chunk.
				@param[in,out] self ECS instance providing archetypes/caches.
				@param[in] policy Execution policy to use for processing.
				@param[in,out] version SystemVersion used to determine dirty chunks and to update after processing.
				@param[in] func Callable to execute for matching entities.
			*/
			template <class Self, typename... Components, typename Func>
			static void forEachPolicyVersionImpl(Self &self, const ExecutionPolicy &policy, SystemVersion &version, Func &&func)
			{
				constexpr ComponentMask requiredRegular{buildRequiredMask<Components...>()};
				const std::vector<Archetype *> &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

				// Collect chunks that need processing (dirty)
				std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> dirtyChunks;

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

						if (version.needsUpdate(chunkVersion, requiredRegular))
						{
							dirtyChunks.emplace_back(arch, chunkIndex, &chunkVersion);
						}
					}
				}

				if (dirtyChunks.empty())
				{
					return;
				}

				// Processing lambda – calls the appropriate chunk processing function
				auto processFunc = [&func](Archetype *arch, ui chunkIndex) {
					ui entityCount{arch->getEntityCount(chunkIndex)};

					if constexpr (std::is_const_v<Self>)
					{
						const Entity *entityArr{arch->getEntityArray(chunkIndex)};
						processChunkEntitiesConst<Components...>(entityArr, entityCount, chunkIndex, arch, std::forward<Func>(func));
					}
					else
					{
						Entity *entityArr{arch->getEntityArray(chunkIndex)};
						processChunkEntities<Components...>(entityArr, entityCount, chunkIndex, arch, std::forward<Func>(func));
					}
				};

				// Helper to update the SystemVersion after processing
				auto updateVersion = [&](const std::tuple<Archetype *, ui, const ChunkVersion *> &chunk) {
					const Archetype *arch{std::get<0>(chunk)};
					const ui chunkIndex{std::get<1>(chunk)};

					const ChunkVersion *chunkVer{std::get<2>(chunk)};

					forEachSetBit(requiredRegular, [&](ComponentTypeID compID) {
						version.setComponentVersion(compID,
													std::max(version.getComponentVersion(compID), chunkVer->getComponentVersion(compID)));
					});

					version.setVersion(std::max(version.getVersion(), arch->getChunkVersion(chunkIndex).getVersion()));
				};

				switch (policy)
				{
					case ExecutionPolicy::Seq:
						forEachPolicyVersionAndVersionCommandSeqImpl(dirtyChunks, processFunc, updateVersion);
						break;
					case ExecutionPolicy::Par:
						forEachPolicyVersionandVersionCommandParImpl(dirtyChunks, self.mThreadPool, processFunc, updateVersion);
						break;
					case ExecutionPolicy::ParBatched:
						forEachPolicyVersionandVersionCommandParBatchedImpl(dirtyChunks, self.mThreadPool, processFunc, updateVersion);
						break;
					case ExecutionPolicy::ParStealing:
						forEachPolicyVersionandVersionCommandParStealingImpl(dirtyChunks, self.mWorkStealingPool, processFunc,
																			 updateVersion);
						break;
					default:
						assert(false && "Invalid execution policy");
				}
			}

			/*! @brief Iterate over entities matching `Components...` that are considered dirty by @p version.
				@tparam Components Component types to query.
				@tparam Func Callable invoked for matching entities.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version SystemVersion used to filter chunks and to update after processing.
				@param[in] func User callable.
			*/
			template <typename... Components, typename Func>
			void forEach(const ExecutionPolicy &policy, SystemVersion &version, Func &&func)
			{
				forEachPolicyVersionImpl<decltype(*this), Components...>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Const overload of versioned `forEach`.
			 */
			template <typename... Components, typename Func>
			void forEach(const ExecutionPolicy &policy, SystemVersion &version, Func &&func) const
			{
				forEachPolicyVersionImpl<decltype(*this), Components...>(*this, policy, version, std::forward<Func>(func));
			}

			// MARK: forEachPolicyVersionCommandImpl

			/*! @brief Dispatches version-filtered processing with a CommandBuffer provided to callbacks.
				@tparam Self The ECS type (possibly const-qualified).
				@tparam Components Component types used to build the required mask.
				@tparam Func Callable invoked per-entity with an additional `CommandBuffer&` parameter.
				@param[in,out] self ECS instance providing archetypes/caches.
				@param[in] policy Execution policy to use for processing.
				@param[in,out] version SystemVersion used to determine dirty chunks and to update after processing.
				@param[in,out] cmds CommandBuffer forwarded to user callbacks.
				@param[in] func User callable invoked for each matching entity and supplied `cmds`.
			*/
			template <class Self, typename... Components, typename Func>
			static void forEachPolicyVersionCommandImpl(Self &self, const ExecutionPolicy &policy, SystemVersion &version,
														CommandBuffer &cmds, Func &&func)
			{
				constexpr ComponentMask requiredRegular{buildRequiredMask<Components...>()};
				const auto &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

				// Collect chunks that are dirty according to the version
				std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> dirtyChunks;

				for (Archetype *arch : matchingArchetypes)
				{
					ui chunkCount{arch->getChunkCount()};

					for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
					{
						if (arch->getEntityCount(chunkIndex) == 0)
						{
							continue;
						}

						const auto &chunkVersion{arch->getChunkVersion(chunkIndex)};

						if (version.needsUpdate(chunkVersion, requiredRegular))
						{
							dirtyChunks.emplace_back(arch, chunkIndex, &chunkVersion);
						}
					}
				}

				if (dirtyChunks.empty())
				{
					return;
				}

				// Processing lambda – dispatches to the correct chunk processing function
				auto processChunk = [&, function = std::forward<Func>(func)](Archetype *arch, ui chunkIndex) {
					ui entityCount{arch->getEntityCount(chunkIndex)};

					if constexpr (std::is_const_v<Self>)
					{
						const Entity *entityArr = arch->getEntityArray(chunkIndex);
						processChunkEntitiesConst<Components...>(
							entityArr, entityCount, chunkIndex, arch,
							[&](const Entity &entity, const auto &...comps) { function(entity, comps..., cmds); });
					}
					else
					{
						Entity *entityArr = arch->getEntityArray(chunkIndex);
						processChunkEntities<Components...>(
							entityArr, entityCount, chunkIndex, arch,
							[&](const Entity &entity, auto &...comps) { function(entity, comps..., cmds); });
					}
				};

				// Helper to update the SystemVersion after processing a chunk
				auto updateVersion = [&](const std::tuple<Archetype *, ui, const ChunkVersion *> &chunk) {
					const Archetype *arch{std::get<0>(chunk)};
					const ui chunkIndex{std::get<1>(chunk)};

					const ChunkVersion *chunkVer{std::get<2>(chunk)};

					forEachSetBit(requiredRegular, [&](const ComponentTypeID &componentTypeID) {
						version.setComponentVersion(componentTypeID, std::max(version.getComponentVersion(componentTypeID),
																			  chunkVer->getComponentVersion(componentTypeID)));
					});

					version.setVersion(std::max(version.getVersion(), arch->getChunkVersion(chunkIndex).getVersion()));
				};

				switch (policy)
				{
					case ExecutionPolicy::Seq:
						forEachPolicyVersionAndVersionCommandSeqImpl(dirtyChunks, processChunk, updateVersion);
						break;
					case ExecutionPolicy::Par:

						forEachPolicyVersionandVersionCommandParImpl(dirtyChunks, self.mThreadPool, processChunk, updateVersion);
						break;
					case ExecutionPolicy::ParBatched:
						forEachPolicyVersionandVersionCommandParBatchedImpl(dirtyChunks, self.mThreadPool, processChunk, updateVersion);
						break;
					case ExecutionPolicy::ParStealing:
						forEachPolicyVersionandVersionCommandParStealingImpl(dirtyChunks, self.mWorkStealingPool, processChunk,
																			 updateVersion);
						break;
					default:
						assert(false && "Invalid execution policy");
				}
			}

			/*! @brief Iterate over entities matching `Components...` that are dirty with respect to @p version and provide a
			   `CommandBuffer` to callbacks, using the specified execution @p policy.
				@tparam Components Component types to query.
				@tparam Func Callable type provided by the user. The callable must be compatible with the command-style processing helpers
			   and may accept signatures such as `(Entity, Components..., CommandBuffer&)` (per-entity) or a per-chunk form that forwards
			   `cmds` to the user's callback.
				@param[in] policy Execution policy controlling parallelism (Seq, Par, ParBatched, ParStealing).
				@param[in,out] version SystemVersion used to filter dirty chunks and updated after processing.
				@param[in,out] cmds CommandBuffer forwarded to user callbacks for recording deferred commands.
				@param[in] func User-provided callable invoked for each matching entity or chunk.
				@note This overload forwards to `forEachPolicyVersionCommandImpl` for dispatch and will update `version` after processing
			   each dirty chunk.
			*/
			template <typename... Components, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
			{
				forEachPolicyVersionCommandImpl<decltype(*this), Components...>(*this, policy, version, cmds, std::forward<Func>(func));
			}

			/*! @brief Const overload of the versioned `forEach` that provides a `CommandBuffer` to callbacks.
				@tparam Components Component types to query (read-only).
				@tparam Func Callable type; the callable must be compatible with the const processing helpers and may accept `(Entity, const
			   Components..., CommandBuffer&)` or a const per-chunk form.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version SystemVersion used to select dirty chunks and updated after processing.
				@param[in,out] cmds CommandBuffer forwarded to user callbacks (may be recorded to even in const systems).
				@param[in] func User-provided callable invoked for each matching entity or chunk; callbacks must not attempt to mutate ECS
			   internal state.
				@note Use this overload for read-only systems that still require issuing commands via `cmds`.
			*/
			template <typename... Components, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
			{
				forEachPolicyVersionCommandImpl<decltype(*this), Components...>(*this, policy, version, cmds, std::forward<Func>(func));
			}

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

			/*! @brief Compute override masks based on provided copy/move arrays.
				@param[in] copyData Array of pointers to copy-source component values.
				@param[in] moveData Array of pointers to move-source component values.
				@param[out] moveOverrideMask Bitmask where move-specified components will be set.
				@param[out] copyOverrideMask Bitmask where copy-specified components will be set.
			*/
			static void acquireOverrideMasks(const std::array<const void *, MAX_COMPONENTS> &copyData,
											 const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask &moveOverrideMask,
											 ComponentMask &copyOverrideMask)
			{
				for (ComponentTypeID componentTypeID{0}; componentTypeID < MAX_COMPONENTS; ++componentTypeID)
				{
					assert(componentTypeID < moveData.size());
					assert(componentTypeID < copyData.size());

					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					if (moveData[componentTypeID] != nullptr)
					{
						if (componentTypeID < LOWER_HALF_BIT_MASK)
						{
							moveOverrideMask.mLow |= (1U << componentTypeID);
						}
						else
						{
							moveOverrideMask.mHigh |= (1U << (componentTypeID - LOWER_HALF_BIT_MASK));
						}
					}
					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					else if (copyData[componentTypeID] != nullptr)
					{
						if (componentTypeID < LOWER_HALF_BIT_MASK)
						{
							copyOverrideMask.mLow |= (1U << componentTypeID);
						}
						else
						{
							copyOverrideMask.mHigh |= (1U << (componentTypeID - LOWER_HALF_BIT_MASK));
						}
					}
				}
			}

			/*! @brief Sets the bit for @p componentTypeID in @p mask (handles low/high bit partition).
				@param[in] componentTypeID Component type identifier.
				@param[in,out] mask ComponentMask to update.
			*/
			static void updateTagMask(const ComponentTypeID componentTypeID, ComponentMask &mask)
			{
				if (componentTypeID < LOWER_HALF_BIT_MASK)
				{
					mask.mLow |= (1U << componentTypeID);
				}
				else
				{
					mask.mHigh |= (1U << (componentTypeID - LOWER_HALF_BIT_MASK));
				}
			}

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

			friend class CommandBuffer;

		private:
			/*! @var mQueryCache
				@brief Cache mapping required component masks to matching archetype lists for fast query resolution.
			*/
			QueryCache mQueryCache{};

			// NOLINTBEGIN(readability-redundant-member-init)

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

			// NOLINTEND(readability-redundant-member-init)

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