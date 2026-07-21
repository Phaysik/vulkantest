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
				requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
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
				requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
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
			static void forEachSeqProcessChunkOnly(const std::vector<Archetype *> &matchingArchetypes, Func &&processChunk)
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
			static void forEachParProcessChunkOnly(const std::vector<Archetype *> &matchingArchetypes, ThreadPool &threadPool,
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
			template <typename Func>
				requires InvocableWithArgs<Func, Archetype *, ui, ui>
			static void forEachParBatchedProcessChunkOnly(const std::vector<Archetype *> &matchingArchetypes, ThreadPool &threadPool,
														  Func &&func)
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
				const FuncT processChunkFunction{std::forward<Func>(func)};

				for (std::size_t i{0}; i < allChunks.size(); i += batchSize)
				{
					const std::size_t end{std::min(i + batchSize, allChunks.size())};
					const std::vector<std::pair<Archetype *, ui>> batch(allChunks.begin() + static_cast<std::ptrdiff_t>(i),
																		allChunks.begin() + static_cast<std::ptrdiff_t>(end));

					threadPool.submitWithLatch(
						[batch, processChunkFunction]() mutable {
							for (const auto &[arch, chunkIndex] : batch)
							{
								const ui entityCount{arch->getEntityCount(chunkIndex)};
								processChunkFunction(arch, chunkIndex, entityCount);
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
			template <typename Func>
				requires InvocableWithArgs<Func, Archetype *, ui, ui>
			static void forEachParStealingProcessChunkOnly(const std::vector<Archetype *> &matchingArchetypes,
														   WorkStealingPool &workStealingPool, Func &&func)
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
						const ui entityCount{arch->getEntityCount(chunkIndex)};

						processChunkFunction(arch, chunkIndex, entityCount);
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

				const Func processChunkFunction{std::forward<Func>(func)};

				auto processChunk = [&](Archetype *arch, const ui chunkIndex, const ui entityCount) {
					if constexpr (std::is_const_v<Self>)
					{
						const Entity *entities{arch->getEntityArray(chunkIndex)};
						processChunkEntitiesConst<Components...>(entities, entityCount, chunkIndex, arch, processChunkFunction);
					}
					else
					{
						Entity *entities{arch->getEntityArray(chunkIndex)};
						processChunkEntities<Components...>(entities, entityCount, chunkIndex, arch, processChunkFunction);
					}
				};

				switch (policy)
				{
					case ExecutionPolicy::Seq:
						forEachSeqProcessChunkOnly(matchingArchetypes, processChunk);
						break;
					case ExecutionPolicy::Par:
						forEachParProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParBatched:
						forEachParBatchedProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParStealing:
						forEachParStealingProcessChunkOnly(matchingArchetypes, self.mWorkStealingPool, processChunk);
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
				requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
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
				requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
			void forEach(const ExecutionPolicy &policy, Func &&func) const
			{
				forEachPolicyImpl<decltype(*this), Components...>(*this, policy, std::forward<Func>(func));
			}

			// MARK: forEachPolicyCommandImpl

			/*! @brief Dispatches command-style processing according to @p policy.
				@tparam Self The ECS type (possibly const-qualified).
				@tparam Components Component types in the query.
				@tparam Func Callable accepting `(Entity, Components...)`.
				@param[in,out] self ECS instance reference.
				@param[in] policy Execution policy to use.
				@param[in] cmds CommandBuffer available for the caller to capture by reference in the callable.
				@param[in] func User callable invoked for each entity or chunk.
				@note The `cmds` parameter is not forwarded into the callback; callers should capture it by reference in their lambda.
			*/
			template <class Self, typename... Components, typename Func>
			static void forEachPolicyCommandImpl(Self &self, const ExecutionPolicy &policy, CommandBuffer & /*cmds*/, Func &&func)
			{
				constexpr ComponentMask requiredRegular{buildRequiredMask<Components...>()};
				const std::vector<Archetype *> &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

				using FuncT = std::decay_t<Func>;
				const FuncT processChunkFunction{std::forward<Func>(func)};

				// Processing lambda – uses the appropriate chunk function based on constness
				auto processChunk = [&, processChunkFunction](Archetype *arch, ui chunkIndex, const ui entityCount) {
					if constexpr (std::is_const_v<Self>)
					{
						const Entity *entities{arch->getEntityArray(chunkIndex)};
						processChunkEntitiesConst<Components...>(
							entities, entityCount, chunkIndex, arch,
							[&](const Entity &entity, const auto &...comps) { processChunkFunction(entity, comps...); });
					}
					else
					{
						Entity *entities{arch->getEntityArray(chunkIndex)};
						processChunkEntitiesConst<Components...>(
							entities, entityCount, chunkIndex, arch,
							[&](Entity &entity, const auto &...comps) { processChunkFunction(entity, comps...); });
					}
				};

				switch (policy)
				{
					case ExecutionPolicy::Seq:
						forEachSeqProcessChunkOnly(matchingArchetypes, processChunk);
						break;
					case ExecutionPolicy::Par:

						forEachParProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParBatched:
						forEachParBatchedProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParStealing:
						forEachParStealingProcessChunkOnly(matchingArchetypes, self.mWorkStealingPool, processChunk);
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
				requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
			void forEach(const ExecutionPolicy &policy, CommandBuffer &cmds, Func &&func)
			{
				forEachPolicyCommandImpl<decltype(*this), Components...>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Const overload of the `forEach` variant that provides a `CommandBuffer` to callbacks.
			 */
			template <typename... Components, typename Func>
				requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
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
			static void forEachSeqProcessChunkAndVersion(const std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> &dirtyChunks,
														 Func &&processFunc, Ver &&updateVersion)
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
			static void forEachParProcessChunkAndVersion(const std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> &dirtyChunks,
														 ThreadPool &threadPool, Func &&processFunc, Ver &&updateVersion)
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
			static void forEachParBatchedProcessChunkAndVersion(
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
			static void forEachParStealingProcessChunkAndVersion(
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

				setDirtyChunks(dirtyChunks, matchingArchetypes, version, requiredRegular);

				if (dirtyChunks.empty())
				{
					return;
				}

				// Processing lambda – adapts (Archetype*, ui) to (Archetype*, ui, ui) for ProcessChunkOnly dispatch
				auto processChunk = [&func](Archetype *arch, const ui chunkIndex, ATTR_MAYBE_UNUSED const ui entityCount) {
					const ui count{arch->getEntityCount(chunkIndex)};

					if constexpr (std::is_const_v<Self>)
					{
						const Entity *entityArr{arch->getEntityArray(chunkIndex)};
						processChunkEntitiesConst<Components...>(entityArr, count, chunkIndex, arch, std::forward<Func>(func));
					}
					else
					{
						Entity *entityArr{arch->getEntityArray(chunkIndex)};
						processChunkEntities<Components...>(entityArr, count, chunkIndex, arch, std::forward<Func>(func));
					}
				};

				switch (policy)
				{
					case ExecutionPolicy::Seq:
						forEachSeqProcessChunkOnly(matchingArchetypes, processChunk);
						break;
					case ExecutionPolicy::Par:
						forEachParProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParBatched:
						forEachParBatchedProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParStealing:
						forEachParStealingProcessChunkOnly(matchingArchetypes, self.mWorkStealingPool, processChunk);
						break;
					default:
						assert(false && "Invalid execution policy");
				}

				bulkMergeVersions(version, dirtyChunks, requiredRegular);
			}

			/*! @brief Iterate over entities matching `Components...` that are considered dirty by @p version.
				@tparam Components Component types to query.
				@tparam Func Callable invoked for matching entities.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version SystemVersion used to filter chunks and to update after processing.
				@param[in] func User callable.
			*/
			template <typename... Components, typename Func>
				requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
			void forEach(const ExecutionPolicy &policy, SystemVersion &version, Func &&func)
			{
				forEachPolicyVersionImpl<decltype(*this), Components...>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Const overload of versioned `forEach`.
			 */
			template <typename... Components, typename Func>
				requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
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
				const std::vector<Archetype *> &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

				// Collect chunks that are dirty according to the version
				std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> dirtyChunks;

				setDirtyChunks(dirtyChunks, matchingArchetypes, version, requiredRegular);

				if (dirtyChunks.empty())
				{
					return;
				}

				using FuncT = std::decay_t<Func>;
				const FuncT processChunkFunction{std::forward<Func>(func)};

				// Processing lambda – adapts to (Archetype*, ui, ui) for ProcessChunkOnly dispatch
				auto processChunk = [&, processChunkFunction](Archetype *arch, const ui chunkIndex, ATTR_MAYBE_UNUSED const ui entityCount) {
					const ui count{arch->getEntityCount(chunkIndex)};

					if constexpr (std::is_const_v<Self>)
					{
						const Entity *entities{arch->getEntityArray(chunkIndex)};
						processChunkEntitiesConst<Components...>(
							entities, count, chunkIndex, arch,
							[&](const Entity &entity, const auto &...comps) { processChunkFunction(entity, comps..., cmds); });
					}
					else
					{
						Entity *entityArr = arch->getEntityArray(chunkIndex);
						processChunkEntities<Components...>(
							entityArr, count, chunkIndex, arch,
							[&](const Entity &entity, auto &...comps) { processChunkFunction(entity, comps..., cmds); });
					}
				};

				switch (policy)
				{
					case ExecutionPolicy::Seq:
						forEachSeqProcessChunkOnly(matchingArchetypes, processChunk);
						break;
					case ExecutionPolicy::Par:
						forEachParProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParBatched:
						forEachParBatchedProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParStealing:
						forEachParStealingProcessChunkOnly(matchingArchetypes, self.mWorkStealingPool, processChunk);
						break;
					default:
						assert(false && "Invalid execution policy");
				}

				bulkMergeVersions(version, dirtyChunks, requiredRegular);
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
				requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
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
				requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
			{
				forEachPolicyVersionCommandImpl<decltype(*this), Components...>(*this, policy, version, cmds, std::forward<Func>(func));
			}

			// MARK: forEachQueryImpl

			/*! @brief Core implementation that executes a query described by three type-lists.
				@details This function builds `ComponentMask` values from the supplied type-lists, constructs a `QueryKey`, queries the
			   archetype cache to find matching archetypes, then dispatches per-chunk processing according to @p policy. The implementation
			   is responsible for selecting const vs non-const per-chunk helpers and for forwarding the user callable into worker tasks when
			   parallel policies are selected. The routine is intended to be instantiated at compile-time for specific type-lists and
			   callables, producing zero-overhead dispatch into per-chunk processing.
				@tparam Self The `ECS` type (possibly const-qualified) used for dispatching and accessing caches/pools.
				@tparam ReqList A `TypeList<...>` containing component/tag types that are required for the query.
				@tparam AnyList A `TypeList<...>` containing component/tag types where any single match satisfies the clause.
				@tparam NoneList A `TypeList<...>` containing component/tag types that must be absent.
				@tparam Func Callable type invoked for matching entities or chunks; this callable is forwarded into the per-chunk processing
			   helpers and must be compatible with the `processChunkEntities...` helpers.
				@param[in,out] self Reference to the `ECS` instance used to resolve archetypes, thread pools, and caches.
				@param[in] policy Execution policy that controls whether processing is sequential or parallel (see `ExecutionPolicy`).
				@param[in] func User-provided callable that will be invoked for matching entities or chunks.
			*/
			template <class Self, typename ReqList, typename AnyList, typename NoneList, typename Func>
			static void forEachQueryImpl(Self &self, ExecutionPolicy &policy, Func &&func)
			{
				constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
				constexpr ComponentMask anyMask{buildMaskFromList<AnyList>()};
				constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};

				QueryKey key{.required = requiredMask, .any = anyMask, .none = noneMask};

				const std::vector<Archetype *> &matchingArchetypes{
					getMatchingArchetypesForQueryCalls<Self, AnyList, NoneList>(self, key, requiredMask, anyMask, noneMask)};

				const Func forwardedFunction{std::forward<Func>(func)};

				// Process each matching archetype (sequential; extend to parallel as needed)
				auto processChunk = [&](Archetype *arch, const ui chunkIndex, const ui entityCount) {
					if constexpr (std::is_const_v<Self>)
					{
						const Entity *entities{arch->getEntityArray(chunkIndex)};

						[&]<typename... Req>(TypeList<Req...>) {
							processChunkEntitiesConst<Req...>(entities, entityCount, chunkIndex, arch, forwardedFunction);
						}(ReqList{});
					}
					else
					{
						Entity *entities{arch->getEntityArray(chunkIndex)};

						[&]<typename... Req>(TypeList<Req...>) {
							processChunkEntities<Req...>(entities, entityCount, chunkIndex, arch, std::forward<Func>(func));
						}(ReqList{});
					}
				};

				switch (policy)
				{
					case ExecutionPolicy::Seq:
						forEachSeqProcessChunkOnly(matchingArchetypes, processChunk);
						break;
					case ExecutionPolicy::Par:
						forEachParProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParBatched:
						forEachParBatchedProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParStealing:
						forEachParStealingProcessChunkOnly(matchingArchetypes, self.mWorkStealingPool, processChunk);
						break;
					default:
						assert(false && "Invalid execution policy");
				}
			}

			/*! @brief Query overload that accepts query clause wrappers (`All<...>`, `Any<...>`, `None<...>`) and executes the supplied
			   callable using the given `ExecutionPolicy`.
				@tparam AllFilter Type of `All<...>` clause specifying required components.
				@tparam AnyFilter Type of `Any<...>` clause specifying optional-match components.
				@tparam NoneFilter Type of `None<...>` clause specifying excluded components.
				@tparam Func Callable type invoked for matching entities or chunks.
				@param[in] policy Execution policy that controls parallelism and batching.
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload transforms the clause wrappers into `TypeList` aliases via `PackExtractor` and forwards to
			   `forEachQueryImpl` which performs archetype matching and dispatch.
			*/
			template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			/*! @brief Const-qualified variant of the query overload that accepts clause wrappers and runs the callable without permitting
			   modification of `ECS` internal state.
				@tparam AllFilter Type of `All<...>` clause specifying required components.
				@tparam AnyFilter Type of `Any<...>` clause specifying optional-match components.
				@tparam NoneFilter Type of `None<...>` clause specifying excluded components.
				@tparam Func Callable type invoked for matching entities or chunks; must be compatible with const processing helpers.
				@param[in] policy Execution policy that controls parallelism and batching.
				@param[in] func User-provided callable forwarded to the underlying const implementation.
				@note Use this overload for read-only systems; it forwards to `forEachQueryImpl` instantiated with a const-qualified `Self`
			   so const-safe processing helpers are selected.
			*/
			template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			// MARK: forEachQueryImpl 2 operator overloads

			/*! @brief Query overload for `All<...>` (required) together with `None<...>` (excluded) clause wrappers.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked for matching entities or chunks.
				@param[in] policy Execution policy to use.
				@param[in] func User-provided callable forwarded to `forEachQueryImpl`.
				@note This overload sets the `Any` clause to empty (`TypeList<>`) and forwards to the core query implementation.
			*/
			template <AllType AllFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>; // empty – no "any" filter
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			/*! @brief Const variant of the `All` + `None` query overload.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
				@param[in] policy Execution policy to use.
				@param[in] func User-provided callable forwarded to the core const implementation.
			*/
			template <AllType AllFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			/*! @brief Query overload for `All<...>` (required) and `Any<...>` (at-least-one) clause wrappers.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
				@tparam Func Callable invoked for matching entities or chunks.
				@param[in] policy Execution policy to use.
				@param[in] func User-provided callable forwarded to `forEachQueryImpl`.
				@note This overload sets the `None` clause to empty (`TypeList<>`).
			*/
			template <AllType AllFilter, AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>; // empty – no "none" filter
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			/*! @brief Const variant of the `All` + `Any` query overload.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
				@tparam Func Callable type compatible with const processing helpers.
				@param[in] policy Execution policy to use.
				@param[in] func User-provided callable forwarded to the core const implementation.
			*/
			template <AllType AllFilter, AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			/*! @brief Query overload for `Any<...>` (at-least-one) together with `None<...>` (excluded) clause wrappers.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked for matching entities or chunks.
				@param[in] policy Execution policy to use.
				@param[in] func User-provided callable forwarded to `forEachQueryImpl`.
				@note This overload sets the `All` (required) clause to empty (`TypeList<>`).
			*/
			template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func)
			{
				using ReqList = TypeList<>; // empty – no required components
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			/*! @brief Const variant of the `Any` + `None` query overload.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable type compatible with const processing helpers.
				@param[in] policy Execution policy to use.
				@param[in] func User-provided callable forwarded to the core const implementation.
			*/
			template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func) const
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			// MARK: forEachQueryImpl 1 operator overloads

			/*! @brief Query overload that iterates entities matching a required-only `All<...>` clause.
				@tparam AllFilter A `All<...>` clause listing required component/tag types.
				@tparam Func Callable invoked for matching entities or chunks; the callable will be forwarded to the mutable processing
			   helpers and may modify ECS state or components.
				@param[in] policy Execution policy controlling parallelism and batching (Seq, Par, ParBatched, ParStealing).
				@param[in] func User-provided callable forwarded to `forEachQueryImpl`.
				@note This overload converts `AllFilter` into a `TypeList` via `PackExtractor` and forwards to the core implementation
				with empty `Any` and `None` clauses.
			*/
			template <AllType AllFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>;
				using NoneList = TypeList<>;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			/*! @brief Const query overload for a required-only `All<...>` clause.
				@tparam AllFilter A `All<...>` clause listing required component/tag types.
				@tparam Func Callable invoked for matching entities or chunks; must be compatible with the const processing helpers.
				@param[in] policy Execution policy to use for processing (Seq, Par, ParBatched, ParStealing).
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload converts `AllFilter` to a `TypeList` via `PackExtractor` and forwards to `forEachQueryImpl` with empty
			   `Any`/`None` clauses.
			*/
			template <AllType AllFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>;
				using NoneList = TypeList<>;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			/*! @brief Query overload that iterates entities matching an `Any<...>` clause (at-least-one match required).
				@tparam AnyFilter An `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
				@tparam Func Callable invoked for matching entities or chunks; the callable will be forwarded to the mutable processing
			   helpers and may modify ECS state or components.
				@param[in] policy Execution policy controlling parallelism and batching (Seq, Par, ParBatched, ParStealing).
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload converts `AnyFilter` into a `TypeList` via `PackExtractor` and forwards to `forEachQueryImpl` with empty
			   `All` and `None` clauses.
			*/
			template <AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func)
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			/*! @brief Const query overload for an `Any<...>` (at-least-one) clause.
				@tparam AnyFilter An `Any<...>` clause listing alternative component/tag types where any one match satisfies the clause.
				@tparam Func Callable invoked for matching entities or chunks; must be compatible with the const processing helpers.
				@param[in] policy Execution policy to use for processing.
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload converts `AnyFilter` to a `TypeList` and uses empty `All`/`None` clauses.
			*/
			template <AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func) const
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			/*! @brief Query overload that iterates entities while excluding types listed in `None<...>`.
				@tparam NoneFilter A `None<...>` clause listing component/tag types that must be absent for a match.
				@tparam Func Callable invoked for matching entities or chunks; the callable will be forwarded to the mutable
				processing helpers and may modify ECS state or components.
				@param[in] policy Execution policy controlling parallelism and batching (Seq, Par, ParBatched, ParStealing).
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload converts `NoneFilter` into a `TypeList` via `PackExtractor` and forwards to `forEachQueryImpl`
				with empty `All` and `Any` clauses.
			*/
			template <NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func)
			{
				using ReqList = TypeList<>;
				using AnyList = TypeList<>;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			/*! @brief Const query overload for a `None<...>` (excluded) clause.
				@tparam NoneFilter A `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked for matching entities or chunks; must be compatible with the const processing helpers.
				@param[in] policy Execution policy to use for processing.
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload converts `NoneFilter` to a `TypeList` and uses empty `All`/`Any` clauses.
			*/
			template <NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func) const
			{
				using ReqList = TypeList<>;
				using AnyList = TypeList<>;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
			}

			// MARK: forEachQueryVersionImpl

			/*! @brief Dispatch and process chunks filtered by a `SystemVersion` (dirty-chunk processing).
				@details Builds masks from the compile-time type lists, finds matching archetypes, collects chunks that are "dirty" with
			   respect to @p version, then processes those chunks and updates the provided `SystemVersion` afterwards. The implementation
			   selects const vs. non-const processing helpers based on whether `Self` is const-qualified and supports sequential and several
			   parallel execution strategies.
				@tparam Self The `ECS` type (possibly const-qualified) used for dispatching and accessing pools/caches.
				@tparam ReqList `TypeList` of required component/tag types.
				@tparam AnyList `TypeList` of alternative types where any single match satisfies the clause.
				@tparam NoneList `TypeList` of excluded component/tag types.
				@tparam Func Callable invoked per entity/chunk; forwarded into the per-chunk processing helpers.
				@param[in,out] self Reference to the `ECS` instance providing archetypes, thread pools and caches.
				@param[in] policy Execution policy controlling parallelism (Seq, Par, ParBatched, ParStealing).
				@param[in,out] version SystemVersion used to determine dirty chunks and updated after processing.
				@param[in] func User callable forwarded into per-chunk processing; must be callable with the signatures used by
				the processing helpers.
				@note Expensive setup (collecting dirty chunks) is performed before dispatch; processing measures and updates component
			   versions via `forEachSetBit` to ensure version state is consistent after completion.
			*/
			template <class Self, typename ReqList, typename AnyList, typename NoneList, typename Func>
			static void forEachQueryVersionImpl(Self &self, const ExecutionPolicy &policy, SystemVersion &version, Func &&func)
			{
				constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
				constexpr ComponentMask anyMask{buildMaskFromList<AnyList>()};
				constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};

				QueryKey key{.required = requiredMask, .any = anyMask, .none = noneMask};

				const std::vector<Archetype *> &matchingArchetypes{
					getMatchingArchetypesForQueryCalls<Self, AnyList, NoneList>(self, key, requiredMask, anyMask, noneMask)};

				// Collect chunks that need processing (dirty)
				std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> dirtyChunks;

				setDirtyChunks(dirtyChunks, matchingArchetypes, version, requiredMask);

				if (dirtyChunks.empty())
				{
					return;
				}

				using FuncT = std::decay_t<Func>;
				const FuncT processChunkFunction{std::forward<Func>(func)};

				auto processChunk = [&, processChunkFunction](Archetype *arch, const ui chunkIndex, ATTR_MAYBE_UNUSED const ui entityCount) {
					const ui count{arch->getEntityCount(chunkIndex)};

					if constexpr (std::is_const_v<Self>)
					{
						const Entity *entities{arch->getEntityArray(chunkIndex)};

						[&]<typename... Req>(TypeList<Req...>) {
							processChunkEntitiesConst<Req...>(
								entities, count, chunkIndex, arch,
								[&](Entity &entity, const auto &...comps) { processChunkFunction(entity, comps...); });
						}(ReqList{});
					}
					else
					{
						Entity *entities{arch->getEntityArray(chunkIndex)};

						[&]<typename... Req>(TypeList<Req...>) {
							processChunkEntities<Req...>(
								entities, count, chunkIndex, arch,
								[&](Entity &entity, const auto &...comps) { processChunkFunction(entity, comps...); });
						}(ReqList{});
					}
				};

				switch (policy)
				{
					case ExecutionPolicy::Seq:
						forEachSeqProcessChunkOnly(matchingArchetypes, processChunk);
						break;
					case ExecutionPolicy::Par:
						forEachParProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParBatched:
						forEachParBatchedProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParStealing:
						forEachParStealingProcessChunkOnly(matchingArchetypes, self.mWorkStealingPool, processChunk);
						break;
					default:
						assert(false && "Invalid execution policy");
				}

				bulkMergeVersions(version, dirtyChunks, requiredMask);
			}

			/*! @brief Iterate over entities matching the provided clause lists but only process chunks that are dirty according to @p
			   version.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (at-least-one match).
				@tparam NoneFilter `None<...>` clause listing excluded component/tag types.
				@tparam Func Callable invoked for matching entities or chunks; will be forwarded into the versioned implementation.
				@param[in] policy Execution policy to use for processing (Seq, Par, ParBatched, ParStealing).
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User callable forwarded to `forEachQueryVersionImpl`.
			*/
			template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Const variant of the versioned `forEach` that processes only dirty chunks determined by @p version.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (at-least-one match).
				@tparam NoneFilter `None<...>` clause listing excluded component/tag types.
				@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
				@param[in] policy Execution policy to use for processing.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User callable forwarded to `forEachQueryVersionImpl`.
				@note Use this overload for read-only systems that still require version-aware processing.
			*/
			template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			// MARK: forEachQueryVersionImpl 2 operator overloads

			/*! @brief Version-aware query overload for `All<...>` + `None<...>` clauses.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked for matching entities or chunks; forwarded to the versioned implementation.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User-provided callable forwarded to `forEachQueryVersionImpl`.
				@note This overload sets the `Any` clause to empty and forwards to the core versioned implementation.
			*/
			template <AllType AllFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>; // empty – no "any" filter
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Const variant of the `All` + `None` versioned overload.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam NoneFilter `None<...>` clause listing excluded component/tag types.
				@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User-provided callable forwarded to the core const implementation.
			*/
			template <AllType AllFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Version-aware query overload for `All<...>` + `Any<...>` clauses.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (at-least-one match).
				@tparam Func Callable invoked for matching entities or chunks; forwarded to the versioned implementation.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User-provided callable forwarded to `forEachQueryVersionImpl`.
				@note This overload sets the `None` clause to empty and forwards to the core versioned implementation.
			*/
			template <AllType AllFilter, AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Const variant of the `All` + `Any` versioned overload.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
				@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User-provided callable forwarded to the core const implementation.
			*/
			template <AllType AllFilter, AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Version-aware query overload for `Any<...>` + `None<...>` clauses.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked for matching entities or chunks; forwarded to the versioned implementation.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User-provided callable forwarded to `forEachQueryVersionImpl`.
				@note This overload sets the `All` (required) clause to empty and forwards to the core versioned implementation.
			*/
			template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Const variant of the `Any` + `None` versioned overload.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User-provided callable forwarded to the core const implementation.
			*/
			template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			// MARK: forEachQueryVersionImpl 1 operator overloads

			/*! @brief Version-aware `forEach` for a required-only `All<...>` clause.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam Func Callable invoked for matching entities or chunks; forwarded to the versioned implementation.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User-provided callable forwarded to `forEachQueryVersionImpl`.
				@note Use this overload for mutable systems that need version-aware (dirty-chunk) processing.
			*/
			template <AllType AllFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>;
				using NoneList = TypeList<>;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Const variant of the required-only versioned `forEach` (`All<...>`).
				@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
				@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User-provided callable forwarded to the const versioned implementation.
			*/
			template <AllType AllFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>;
				using NoneList = TypeList<>;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Version-aware `forEach` for an `Any<...>` (at-least-one) clause.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
				@tparam Func Callable invoked for matching entities or chunks; forwarded to the versioned implementation.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User-provided callable forwarded to `forEachQueryVersionImpl`.
				@note Use this overload when a match requires at least one of the listed component types.
			*/
			template <AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Const variant of the `Any<...>` versioned `forEach`.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only).
				@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User-provided callable forwarded to the const versioned implementation.
			*/
			template <AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Version-aware `forEach` that excludes component types listed in `None<...>`.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent for a match.
				@tparam Func Callable invoked for matching entities or chunks; forwarded to the versioned implementation.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User-provided callable forwarded to `forEachQueryVersionImpl`.
				@note This overload is useful for queries that only exclude specific components.
			*/
			template <NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
			{
				using ReqList = TypeList<>;
				using AnyList = TypeList<>;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			/*! @brief Const variant of the `None<...>` versioned `forEach`.
				@tparam NoneFilter `None<...>` clause listing excluded component/tag types (read-only).
				@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] func User-provided callable forwarded to the const versioned implementation.
			*/
			template <NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
			{
				using ReqList = TypeList<>;
				using AnyList = TypeList<>;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
			}

			// MARK: forEachQueryCommandImpl

			/*! @brief Dispatches processing with a `CommandBuffer` available for capture.
				@details Builds compile-time masks from the supplied type-lists, queries the archetype cache for matches, and then processes
			   matching chunks using the selected `ExecutionPolicy`.
				@tparam Self The `ECS` type (possibly const-qualified) used for dispatch and to access thread pools/caches.
				@tparam ReqList `TypeList` of required component/tag types.
				@tparam AnyList `TypeList` of types where any single match satisfies the clause.
				@tparam NoneList `TypeList` of component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk.
				@param[in,out] self ECS instance providing archetypes, thread pools and caches.
				@param[in] policy Execution policy controlling parallelism (Seq, Par, ParBatched, ParStealing).
				@param[in] cmds `CommandBuffer` available for the caller to capture by reference in the callable.
				@param[in] func User callable forwarded into per-chunk processing; compatible with `processChunkEntities...` helpers.
				@note The `cmds` parameter is not forwarded into the callback; callers should capture it by reference in their lambda.
			*/
			template <class Self, typename ReqList, typename AnyList, typename NoneList, typename Func>
			static void forEachQueryCommandImpl(Self &self, ExecutionPolicy &policy, CommandBuffer & /*cmds*/, Func &&func)
			{
				constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
				constexpr ComponentMask anyMask{buildMaskFromList<AnyList>()};
				constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};

				QueryKey key{.required = requiredMask, .any = anyMask, .none = noneMask};

				const std::vector<Archetype *> &matchingArchetypes{
					getMatchingArchetypesForQueryCalls<Self, AnyList, NoneList>(self, key, requiredMask, anyMask, noneMask)};

				using FuncT = std::decay_t<Func>;
				const FuncT processChunkFunction{std::forward<Func>(func)};

				// Process each matching archetype (sequential; extend to parallel as needed)
				auto processChunk = [&, processChunkFunction](Archetype *arch, const ui chunkIndex, const ui entityCount) {
					if constexpr (std::is_const_v<Self>)
					{
						const Entity *entities{arch->getEntityArray(chunkIndex)};

						[&]<typename... Req>(TypeList<Req...>) {
							processChunkEntitiesConst<Req...>(
								entities, entityCount, chunkIndex, arch,
								[&](const Entity &entity, const auto &...comps) { processChunkFunction(entity, comps...); });
						}(ReqList{});
					}
					else
					{
						Entity *entities{arch->getEntityArray(chunkIndex)};

						[&]<typename... Req>(TypeList<Req...>) {
							processChunkEntities<Req...>(
								entities, entityCount, chunkIndex, arch,
								[&](Entity &entity, const auto &...comps) { processChunkFunction(entity, comps...); });
						}(ReqList{});
					}
				};

				switch (policy)
				{
					case ExecutionPolicy::Seq:
						forEachSeqProcessChunkOnly(matchingArchetypes, processChunk);
						break;
					case ExecutionPolicy::Par:
						forEachParProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParBatched:
						forEachParBatchedProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParStealing:
						forEachParStealingProcessChunkOnly(matchingArchetypes, self.mWorkStealingPool, processChunk);
						break;
					default:
						assert(false && "Invalid execution policy");
				}
			}

			/*! @brief Command-style overload that accepts clause wrappers (`All`, `Any`, `None`) and forwards a `CommandBuffer`.
				@tparam AllFilter `All<...>` clause specifying required types.
				@tparam AnyFilter `Any<...>` clause specifying alternative-match types.
				@tparam NoneFilter `None<...>` clause specifying excluded types.
				@tparam Func Callable invoked per-entity or per-chunk; must accept a `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload transforms clause wrappers into `TypeList` aliases via `PackExtractor` and forwards to
			   `forEachQueryCommandImpl`.
			*/
			template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Const-qualified command-style overload that accepts clause wrappers and forwards a `CommandBuffer`.
				@tparam AllFilter `All<...>` clause specifying required types (read-only).
				@tparam AnyFilter `Any<...>` clause specifying alternative-match types.
				@tparam NoneFilter `None<...>` clause specifying excluded types.
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used in per-entity form.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying const implementation.
			*/
			template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			// MARK: forEachQueryCommandImpl 2 operator overloads

			/*! @brief Command-style `forEach` overload for `All<...>` + `None<...>` clauses.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload sets the `Any` clause to empty (`TypeList<>`) and forwards to `forEachQueryCommandImpl`.
			*/
			template <AllType AllFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>; // empty – no "any" filter
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Const-qualified command-style `forEach` overload for `All<...>` + `None<...>` clauses.
				@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the const implementation.
				@note This overload sets the `Any` clause to empty (`TypeList<>`) and forwards to the const `forEachQueryCommandImpl`.
			*/
			template <AllType AllFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Command-style `forEach` overload for `All<...>` + `Any<...>` clauses.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload sets the `None` clause to empty (`TypeList<>`) and forwards to `forEachQueryCommandImpl`.
			*/
			template <AllType AllFilter, AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>; // empty – no "none" filter
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Const-qualified command-style `forEach` overload for `All<...>` + `Any<...>` clauses.
				@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the const implementation.
				@note This overload sets the `None` clause to empty (`TypeList<>`) and forwards to the const `forEachQueryCommandImpl`.
			*/
			template <AllType AllFilter, AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Command-style `forEach` overload for `Any<...>` + `None<...>` clauses.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload sets the `All` (required) clause to empty (`TypeList<>`) and forwards to `forEachQueryCommandImpl`.
			*/
			template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = TypeList<>; // empty – no required components
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Const-qualified command-style `forEach` overload for `Any<...>` + `None<...>` clauses.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only).
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the const implementation.
				@note This overload sets the `All` (required) clause to empty (`TypeList<>`) and forwards to the const
			   `forEachQueryCommandImpl`.
			*/
			template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			// MARK: forEachQueryCommandImpl 1 operator overloads

			/*! @brief Command-style `forEach` for a required-only `All<...>` clause.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload converts `AllFilter` to a `TypeList` via `PackExtractor` and forwards to `forEachQueryCommandImpl` with
			   empty `Any`/`None` clauses.
			*/
			template <AllType AllFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>;
				using NoneList = TypeList<>;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Const-qualified command-style `forEach` for a required-only `All<...>` clause.
				@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the const implementation.
			*/
			template <AllType AllFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>;
				using NoneList = TypeList<>;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Command-style `forEach` for an `Any<...>` (at-least-one) clause.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload converts `AnyFilter` to a `TypeList` via `PackExtractor` and forwards to `forEachQueryCommandImpl` with
			   empty `All`/`None` clauses.
			*/
			template <AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Const-qualified command-style `forEach` for an `Any<...>` (at-least-one) clause.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only).
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the const implementation.
			*/
			template <AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Command-style `forEach` that excludes types listed in `None<...>`.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent for a match.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying implementation.
				@note This overload converts `NoneFilter` to a `TypeList` via `PackExtractor` and forwards to `forEachQueryCommandImpl` with
			   empty `All`/`Any` clauses.
			*/
			template <NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = TypeList<>;
				using AnyList = TypeList<>;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			/*! @brief Const-qualified command-style `forEach` that excludes types listed in `None<...>`.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent (read-only).
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the const implementation.
			*/
			template <NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = TypeList<>;
				using AnyList = TypeList<>;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
			}

			// MARK: forEachQueryVersionCommandImpl

			/*! @brief Version-aware dispatch with a `CommandBuffer` available for capture.
				@details Builds compile-time masks from the supplied type-lists, queries the archetype cache for matches, collects chunks
			   that are dirty according to @p version, then processes those chunks and updates @p version afterwards.
				@tparam Self The `ECS` type (possibly const-qualified) used for dispatch and to access pools/caches.
				@tparam ReqList `TypeList` of required component/tag types.
				@tparam AnyList `TypeList` of alternative types where any single match satisfies the clause.
				@tparam NoneList `TypeList` of component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk.
				@param[in,out] self Reference to the `ECS` instance providing archetypes, thread pools and caches.
				@param[in] policy Execution policy controlling parallelism (Seq, Par, ParBatched, ParStealing).
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in] cmds `CommandBuffer` available for the caller to capture by reference in the callable.
				@param[in] func User callable forwarded into per-chunk processing; compatible with the per-chunk helpers.
				@note The `cmds` parameter is not forwarded into the callback; callers should capture it by reference in their lambda.
			*/
			template <class Self, typename ReqList, typename AnyList, typename NoneList, typename Func>
			static void forEachQueryVersionCommandImpl(Self &self, ExecutionPolicy &policy, SystemVersion &version,
													   CommandBuffer & /*cmds*/, Func &&func)
			{
				constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
				constexpr ComponentMask anyMask{buildMaskFromList<AnyList>()};
				constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};

				QueryKey key{.required = requiredMask, .any = anyMask, .none = noneMask};

				const std::vector<Archetype *> &matchingArchetypes{
					getMatchingArchetypesForQueryCalls<Self, AnyList, NoneList>(self, key, requiredMask, anyMask, noneMask)};

				// Collect chunks that are dirty according to the version
				std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> dirtyChunks;

				setDirtyChunks(dirtyChunks, matchingArchetypes, version, requiredMask);

				if (dirtyChunks.empty())
				{
					return;
				}

				using FuncT = std::decay_t<Func>;
				const FuncT processChunkFunction{std::forward<Func>(func)};

				auto processChunk = [&, processChunkFunction](Archetype *arch, const ui chunkIndex, ATTR_MAYBE_UNUSED const ui entityCount) {
					const ui count{arch->getEntityCount(chunkIndex)};

					if constexpr (std::is_const_v<Self>)
					{
						const Entity *entities{arch->getEntityArray(chunkIndex)};

						[&]<typename... Req>(TypeList<Req...>) {
							processChunkEntitiesConst<Req...>(
								entities, count, chunkIndex, arch,
								[&](Entity &entity, const auto &...comps) { processChunkFunction(entity, comps...); });
						}(ReqList{});
					}
					else
					{
						Entity *entities{arch->getEntityArray(chunkIndex)};

						[&]<typename... Req>(TypeList<Req...>) {
							processChunkEntities<Req...>(
								entities, count, chunkIndex, arch,
								[&](Entity &entity, const auto &...comps) { processChunkFunction(entity, comps...); });
						}(ReqList{});
					}
				};

				switch (policy)
				{
					case ExecutionPolicy::Seq:
						forEachSeqProcessChunkOnly(matchingArchetypes, processChunk);
						break;
					case ExecutionPolicy::Par:
						forEachParProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParBatched:
						forEachParBatchedProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
						break;
					case ExecutionPolicy::ParStealing:
						forEachParStealingProcessChunkOnly(matchingArchetypes, self.mWorkStealingPool, processChunk);
						break;
					default:
						assert(false && "Invalid execution policy");
				}

				bulkMergeVersions(version, dirtyChunks, requiredMask);
			}

			/*! @brief Version-aware command-style `forEach` that accepts clause wrappers and a `CommandBuffer`.
				@tparam AllFilter `All<...>` clause specifying required types.
				@tparam AnyFilter `Any<...>` clause specifying alternative-match types.
				@tparam NoneFilter `None<...>` clause specifying excluded types.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying versioned implementation.
				@note Transforms clause wrappers into `TypeList` aliases via `PackExtractor` and forwards to
			   `forEachQueryVersionCommandImpl` which performs dirty-chunk selection and processing.
			*/
			template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			/*! @brief Const-qualified version-aware command-style `forEach` that accepts clause wrappers and a `CommandBuffer`.
				@tparam AllFilter `All<...>` clause specifying required types (read-only).
				@tparam AnyFilter `Any<...>` clause specifying alternative-match types.
				@tparam NoneFilter `None<...>` clause specifying excluded types.
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying const versioned implementation.
			*/
			template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			// MARK: forEachQueryVersionCommandImpl 2 operator overloads

			/*! @brief Version-aware command-style `forEach` for `All<...>` + `None<...>` clauses.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying versioned implementation.
				@note This overload sets the `Any` clause to empty (`TypeList<>`) and forwards to `forEachQueryVersionCommandImpl`.
			*/
			template <AllType AllFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>; // empty – no "any" filter
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			/*! @brief Const-qualified version-aware command-style `forEach` for `All<...>` + `None<...>` clauses.
				@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the const versioned implementation.
			*/
			template <AllType AllFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			/*! @brief Version-aware command-style `forEach` for `All<...>` + `Any<...>` clauses.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying versioned implementation.
				@note This overload sets the `None` clause to empty (`TypeList<>`) and forwards to `forEachQueryVersionCommandImpl`.
			*/
			template <AllType AllFilter, AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>; // empty – no "none" filter
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			/*! @brief Const-qualified version-aware command-style `forEach` for `All<...>` + `Any<...>` clauses.
				@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying const versioned implementation.
			*/
			template <AllType AllFilter, AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			/*! @brief Version-aware command-style `forEach` for `Any<...>` + `None<...>` clauses.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying versioned implementation.
				@note This overload sets the `All` (required) clause to empty (`TypeList<>`) and forwards to
			   `forEachQueryVersionCommandImpl`.
			*/
			template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = TypeList<>; // empty – no required components
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			/*! @brief Const-qualified version-aware command-style `forEach` for `Any<...>` + `None<...>` clauses.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only).
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying const versioned implementation.
			*/
			template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			// MARK: forEachQueryVersionCommandImpl 1 operator overloads

			/*! @brief Version-aware command-style `forEach` for a required-only `All<...>` clause.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying versioned implementation.
				@note This overload converts `AllFilter` to a `TypeList` via `PackExtractor` and forwards to
			   `forEachQueryVersionCommandImpl` with empty `Any`/`None` clauses.
			*/
			template <AllType AllFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>;
				using NoneList = TypeList<>;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			/*! @brief Const-qualified version-aware command-style `forEach` for a required-only `All<...>` clause.
				@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying const versioned implementation.
			*/
			template <AllType AllFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = PackExtractor<AllFilter>::type;
				using AnyList = TypeList<>;
				using NoneList = TypeList<>;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			/*! @brief Version-aware command-style `forEach` for an `Any<...>` (at-least-one) clause.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying versioned implementation.
				@note This overload converts `AnyFilter` to a `TypeList` via `PackExtractor` and forwards to
			   `forEachQueryVersionCommandImpl` with empty `All`/`None` clauses.
			*/
			template <AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			/*! @brief Const-qualified version-aware command-style `forEach` for an `Any<...>` clause.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only).
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying const versioned implementation.
			*/
			template <AnyType AnyFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = TypeList<>;
				using AnyList = PackExtractor<AnyFilter>::type;
				using NoneList = TypeList<>;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			/*! @brief Version-aware command-style `forEach` that excludes types listed in `None<...>`.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent for a match.
				@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying versioned implementation.
				@note This overload converts `NoneFilter` to a `TypeList` via `PackExtractor` and forwards to
			   `forEachQueryVersionCommandImpl` with empty `All`/`Any` clauses.
			*/
			template <NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
			{
				using ReqList = TypeList<>;
				using AnyList = TypeList<>;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			/*! @brief Const-qualified version-aware command-style `forEach` that excludes types listed in `None<...>`.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent (read-only).
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
			   `CommandBuffer&` when used per-entity.
				@param[in] policy Execution policy controlling parallelism and batching.
				@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
				@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
				@param[in] func User-provided callable forwarded to the underlying const versioned implementation.
			*/
			template <NoneType NoneFilter, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
			{
				using ReqList = TypeList<>;
				using AnyList = TypeList<>;
				using NoneList = PackExtractor<NoneFilter>::type;
				forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds,
																							std::forward<Func>(func));
			}

			// MARK: forEachQuery

			/*! @brief Convenience `forEach` that runs the query with `ExecutionPolicy::Seq`.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk. Forwarded to the underlying `forEach` overload.
				@param[in] func User-provided callable forwarded to the sequential `forEach` implementation.
				@note This overload is a convenience wrapper that selects the sequential execution policy.
			*/
			template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(Func &&func)
			{
				forEach<AllFilter, AnyFilter, NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			/*! @brief Const-qualified convenience `forEach` that runs the query with `ExecutionPolicy::Seq`.
				@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
				@param[in] func User-provided callable forwarded to the sequential const `forEach` implementation.
				@note This overload is a const convenience wrapper that selects the sequential execution policy.
			*/
			template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(Func &&func) const
			{
				forEach<AllFilter, AnyFilter, NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			// MARK: forEachQuery 2 operator overloads

			/*! @brief Convenience `forEach` (required + excluded) using `ExecutionPolicy::Seq`.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; forwarded to the underlying implementation.
				@param[in] func User-provided callable forwarded to the sequential `forEach` implementation.
				@note This is a convenience wrapper that selects the sequential execution policy.
			*/
			template <AllType AllFilter, NoneType NoneFilter, typename Func>
			void forEach(Func &&func)
			{
				forEach<AllFilter, NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			/*! @brief Const-qualified convenience `forEach` (required + excluded) using `ExecutionPolicy::Seq`.
				@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
				@param[in] func User-provided callable forwarded to the sequential const `forEach` implementation.
			*/
			template <AllType AllFilter, NoneType NoneFilter, typename Func>
			void forEach(Func &&func) const
			{
				forEach<AllFilter, NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			/*! @brief Convenience `forEach` (required + alternative) using `ExecutionPolicy::Seq`.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
				@tparam Func Callable invoked per-entity or per-chunk; forwarded to the underlying implementation.
				@param[in] func User-provided callable forwarded to the sequential `forEach` implementation.
			*/
			template <AllType AllFilter, AnyType AnyFilter, typename Func>
			void forEach(Func &&func)
			{
				forEach<AllFilter, AnyFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			/*! @brief Const-qualified convenience `forEach` (required + alternative) using `ExecutionPolicy::Seq`.
				@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
			*/
			template <AllType AllFilter, AnyType AnyFilter, typename Func>
			void forEach(Func &&func) const
			{
				forEach<AllFilter, AnyFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			/*! @brief Convenience `forEach` (alternative + excluded) using `ExecutionPolicy::Seq`.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; forwarded to the underlying implementation.
			*/
			template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(Func &&func)
			{
				forEach<AnyFilter, NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			/*! @brief Const-qualified convenience `forEach` (alternative + excluded) using `ExecutionPolicy::Seq`.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only where applicable).
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
			*/
			template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
			void forEach(Func &&func) const
			{
				forEach<AnyFilter, NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			// MARK: forEachQuery 1 operator overloads

			/*! @brief Convenience `forEach` for a required-only `All<...>` clause using `ExecutionPolicy::Seq`.
				@tparam AllFilter `All<...>` clause listing required component/tag types.
				@tparam Func Callable invoked per-entity or per-chunk; forwarded to the underlying implementation.
				@param[in] func User-provided callable forwarded to the sequential `forEach` implementation.
				@note This overload is a convenience wrapper that selects the sequential execution policy.
			*/
			template <AllType AllFilter, typename Func>
			void forEach(Func &&func)
			{
				forEach<AllFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			/*! @brief Const-qualified convenience `forEach` for a required-only `All<...>` clause using `ExecutionPolicy::Seq`.
				@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
			*/
			template <AllType AllFilter, typename Func>
			void forEach(Func &&func) const
			{
				forEach<AllFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			/*! @brief Convenience `forEach` for an `Any<...>` clause (at-least-one match) using `ExecutionPolicy::Seq`.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
				@tparam Func Callable invoked per-entity or per-chunk; forwarded to the underlying implementation.
			*/
			template <AnyType AnyFilter, typename Func>
			void forEach(Func &&func)
			{
				forEach<AnyFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			/*! @brief Const-qualified convenience `forEach` for an `Any<...>` clause using `ExecutionPolicy::Seq`.
				@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only where applicable).
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
			*/
			template <AnyType AnyFilter, typename Func>
			void forEach(Func &&func) const
			{
				forEach<AnyFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			/*! @brief Convenience `forEach` that excludes types listed in `None<...>` using `ExecutionPolicy::Seq`.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent for a match.
				@tparam Func Callable invoked per-entity or per-chunk; forwarded to the underlying implementation.
			*/
			template <NoneType NoneFilter, typename Func>
			void forEach(Func &&func)
			{
				forEach<NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			/*! @brief Const-qualified convenience `forEach` that excludes types listed in `None<...>` using `ExecutionPolicy::Seq`.
				@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent (read-only where applicable).
				@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
			*/
			template <NoneType NoneFilter, typename Func>
			void forEach(Func &&func) const
			{
				forEach<NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
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
				if (componentTypeID < LOWER_HALF_BIT_MASK)
				{
					mask.mLow |= (1U << componentTypeID);
				}
				else
				{
					mask.mHigh |= (1U << (componentTypeID - LOWER_HALF_BIT_MASK));
				}
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
																				const ComponentMask &anyMask, const ComponentMask &noneMask)
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
									   const ComponentMask &requiredRegular)
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

						if (version.needsUpdate(chunkVersion, requiredRegular))
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

		private:
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