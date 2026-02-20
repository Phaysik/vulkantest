/*! \file ecs.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_ECS_H
#define INCLUDE_ECS_ECS_H

#include <algorithm>
#include <latch>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

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

	using Dimensia::Core::ui;

	enum class ExecutionPolicy : Dimensia::Core::ub
	{
		Seq,
		Par,
		ParBatched,
		ParStealing
	};

	class ECS
	{
		public:
			// MARK: Constructor, Assignment Operators, and Destructor

			explicit ECS();

			ECS(const ECS &) = delete;
			ECS &operator=(const ECS &) = delete;
			ECS(ECS &&) = delete;
			ECS &operator=(ECS &&) = delete;

			// NOLINTNEXTLINE(hicpp-use-equals-default,modernize-use-equals-default)
			~ECS() {}

			// MARK: Getters

			std::vector<Entity> getChildren(const Entity &parent) const;

			Entity getParent(const Entity &child) const;

			// MARK: Setter

			void setParent(const Entity &child, const Entity &parent);

			// MARK: Member Functions

			Entity createEntity();

			void destroyEntity(const Entity &entity, const bool destroyChildren = true);

			bool alive(const Entity &entity) const noexcept;

			void compact();

			void removeComponent(const Entity &entity, const ComponentTypeID compID);

			// MARK: Template Member Functions

			template <typename... Ts>
			Entity createEntityWith(Ts &&...components)
			{
				std::array<ComponentTypeID, sizeof...(Ts)> compIds{componentId<std::decay_t<Ts>>()...};

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
						 const ComponentTypeID componentTypeID{compIds[I]};

						 assert(componentTypeID < ComponentInfos.size());

						 // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
						 const ComponentInfo &info{ComponentInfos[componentTypeID]};

						 if (info.isTag)
						 {
							 if (componentTypeID < LOWER_HALF_BIT_MASK)
							 {
								 tagMask.mLow |= (1U << componentTypeID);
							 }
							 else
							 {
								 tagMask.mHigh |= (1U << (componentTypeID - LOWER_HALF_BIT_MASK));
							 }
						 }
						 else
						 {
							 if (componentTypeID < LOWER_HALF_BIT_MASK)
							 {
								 regularMask.mLow |= (1U << componentTypeID);
							 }
							 else
							 {
								 regularMask.mHigh |= (1U << (componentTypeID - LOWER_HALF_BIT_MASK));
							 }
							 if constexpr (std::is_const_v<std::remove_reference_t<decltype(components)>>)
							 {
								 assert(componentTypeID < copyData.size());

								 // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
								 copyData[componentTypeID] = &std::get<I>(storage);
							 }
							 else
							 {
								 assert(componentTypeID < copyData.size());

								 // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
								 moveData[componentTypeID] = &std::get<I>(storage);
							 }
						 }
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
			template <typename T>
			void addComponent(Entity entity, T value)
			{
				if (!alive(entity))
				{
					return;
				}

				ComponentTypeID compID{componentId<T>()};

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

			template <typename T>
			void removeComponent(Entity entity)
			{
				if (!alive(entity))
				{
					return;
				}

				ComponentTypeID compID{componentId<T>()};

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

			template <typename T>
			T *getComponent(Entity entity)
			{
				return static_cast<T *>(getComponentPtr(entity, componentId<T>()));
			}

			template <typename T>
			const T *getComponent(Entity entity) const
			{
				return static_cast<const T *>(getComponentPtr(entity, componentId<T>()));
			}

			// Tag management
			template <typename Tag>
			void addTag(Entity entity)
			{
				if (!alive(entity))
				{
					return;
				}

				ComponentTypeID tagID{componentId<Tag>()};

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

			template <typename Tag>
			void removeTag(Entity entity)
			{
				if (!alive(entity))
				{
					return;
				}

				ComponentTypeID tagID{componentId<Tag>()};

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

			template <typename Tag>
			bool hasTag(Entity entity) const
			{
				if (!alive(entity))
				{
					return false;
				}

				ComponentTypeID tagID{componentId<Tag>()};

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

			// Queries (forEach)
			template <typename... Components, typename Func>
			void forEach(Func &&func)
			{
				forEach<Components...>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			template <typename... Components, typename Func>
			void forEach(Func &&func) const
			{
				forEach<Components...>(ExecutionPolicy::Seq, std::forward<Func>(func));
			}

			template <class Self, typename... Components, typename Func>
			static void forEachPolicyImpl(Self &self, const ExecutionPolicy &policy, Func &&func)
			{
				constexpr ComponentMask requiredRegular{build_required_mask<Components...>()};
				const auto &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

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

				if (policy == ExecutionPolicy::Seq)
				{
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

							processChunk(arch, chunkIndex, entityCount);
						}
					}
				}
				else if (policy == ExecutionPolicy::Par)
				{
					std::vector<std::future<void>> futures;
					futures.reserve(matchingArchetypes.size() * 2);

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

							futures.push_back(self.mThreadPool.submit(processChunk, arch, chunkIndex, entityCount));
						}
					}

					for (auto &fut : futures)
					{
						fut.get();
					}
				}
				else if (policy == ExecutionPolicy::ParBatched)
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

					// Adaptive batch size based on hardware concurrency
					const std::size_t numThreads{std::thread::hardware_concurrency()};
					const std::size_t targetTasks{numThreads * 4};

					std::size_t batchSize{(allChunks.size() + targetTasks - 1) / targetTasks};

					batchSize = std::max<std::size_t>(batchSize, 1);

					std::latch latch(static_cast<std::ptrdiff_t>((allChunks.size() + batchSize - 1) / batchSize));

					for (std::size_t i{0}; i < allChunks.size(); i += batchSize)
					{
						const std::size_t end{std::min(i + batchSize, allChunks.size())};
						const std::vector<std::pair<Archetype *, ui>> batch(allChunks.begin() + static_cast<std::ptrdiff_t>(i),
																			allChunks.begin() + static_cast<std::ptrdiff_t>(end));

						self.mThreadPool.submit_with_latch(
							[batch, func = std::forward<Func>(func)]() mutable {
								for (const auto &[arch, chunkIndex] : batch)
								{
									ui entityCount{arch->getEntityCount(chunkIndex)};
									Entity *entityArr = arch->getEntityArray(chunkIndex);
									processChunkEntities<Components...>(entityArr, entityCount, chunkIndex, arch, func);
								}
							},
							latch);
					}
					latch.wait();
				}
				else if (policy == ExecutionPolicy::ParStealing)
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

					// Use the work stealing pool; batch size may be adaptive but we keep it simple
					const std::size_t batchSize{8}; // could also compute adaptively
					std::latch latch(static_cast<std::ptrdiff_t>((allChunks.size() + batchSize - 1) / batchSize));

					self.mWorkStealingPool.submit_chunks(
						allChunks,
						[func = std::forward<Func>(func)](Archetype *arch, ui chunkIndex) {
							ui entityCount{arch->getEntityCount(chunkIndex)};
							Entity *entityArr = arch->getEntityArray(chunkIndex);
							processChunkEntities<Components...>(entityArr, entityCount, chunkIndex, arch, func);
						},
						latch, batchSize);
					latch.wait();
				}
			}

			template <typename... Components, typename Func>
			void forEach(const ExecutionPolicy &policy, Func &&func)

			{
				forEachPolicyImpl<decltype(*this), Components...>(*this, policy, std::forward<Func>(func));
			}

			template <typename... Components, typename Func>
			void forEach(const ExecutionPolicy &policy, Func &&func) const
			{
				forEachPolicyImpl<decltype(*this), Components...>(*this, policy, std::forward<Func>(func));
			}

			template <class Self, typename... Components, typename Func>
			static void forEachPolicyVersionImpl(Self &self, const ExecutionPolicy &policy, SystemVersion &version, Func &&func)
			{
				constexpr ComponentMask requiredRegular{build_required_mask<Components...>()};
				const auto &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

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

				if (policy == ExecutionPolicy::Seq)
				{
					for (const auto &chunk : dirtyChunks)
					{
						processFunc(std::get<0>(chunk), std::get<1>(chunk));
						updateVersion(chunk);
					}
				}
				else if (policy == ExecutionPolicy::Par)
				{
					std::vector<std::future<void>> futures;
					futures.reserve(dirtyChunks.size());

					for (const auto &chunk : dirtyChunks)
					{
						Archetype *arch{std::get<0>(chunk)};
						ui chunkIndex{std::get<1>(chunk)};

						futures.push_back(self.mThreadPool.submit([arch, chunkIndex, &processFunc]() { processFunc(arch, chunkIndex); }));
					}

					for (auto &fut : futures)
					{
						fut.get();
					}

					for (const auto &chunk : dirtyChunks)
					{
						updateVersion(chunk);
					}
				}
				else if (policy == ExecutionPolicy::ParBatched)
				{
					// Convert to simple pairs for batching
					std::vector<std::pair<Archetype *, ui>> chunks;
					chunks.reserve(dirtyChunks.size());

					for (const auto &chunk : dirtyChunks)
					{
						chunks.emplace_back(std::get<0>(chunk), std::get<1>(chunk));
					}

					// Adaptive batch size (similar to forEachPolicyImpl)
					const std::size_t numThreads{std::thread::hardware_concurrency()};
					const std::size_t targetTasks{numThreads * 4};

					std::size_t batchSize{(chunks.size() + targetTasks - 1) / targetTasks};
					batchSize = std::max<std::size_t>(batchSize, 1);

					std::latch latch(static_cast<std::ptrdiff_t>((chunks.size() + batchSize - 1) / batchSize));

					for (std::size_t i{0}; i < chunks.size(); i += batchSize)
					{
						const std::size_t end{std::min(i + batchSize, chunks.size())};
						const std::vector<std::pair<Archetype *, ui>> batch(chunks.begin() + static_cast<std::ptrdiff_t>(i),
																			chunks.begin() + static_cast<std::ptrdiff_t>(end));

						self.mThreadPool.submit_with_latch(
							[batch, &processFunc]() {
								for (const auto &[arch, chunkIndex] : batch)
								{
									processFunc(arch, chunkIndex);
								}
							},
							latch);
					}

					latch.wait();

					for (const auto &chunk : dirtyChunks)
					{
						updateVersion(chunk);
					}
				}
				else if (policy == ExecutionPolicy::ParStealing)
				{
					std::vector<std::pair<Archetype *, ui>> chunks;
					chunks.reserve(dirtyChunks.size());

					for (const auto &chunk : dirtyChunks)
					{
						chunks.emplace_back(std::get<0>(chunk), std::get<1>(chunk));
					}

					const std::size_t batchSize{8}; // same as in forEachPolicyImpl
					std::latch latch(static_cast<std::ptrdiff_t>((chunks.size() + batchSize - 1) / batchSize));

					self.mWorkStealingPool.submit_chunks(
						chunks, [&processFunc](Archetype *arch, ui chunkIndex) { processFunc(arch, chunkIndex); }, latch, batchSize);

					latch.wait();

					for (const auto &chunk : dirtyChunks)
					{
						updateVersion(chunk);
					}
				}
			}

			template <typename... Components, typename Func>
			void forEach(const ExecutionPolicy &policy, SystemVersion &version, Func &&func)
			{
				forEachPolicyVersionImpl<decltype(*this), Components...>(*this, policy, version, std::forward<Func>(func));
			}

			template <typename... Components, typename Func>
			void forEach(const ExecutionPolicy &policy, SystemVersion &version, Func &&func) const
			{
				forEachPolicyVersionImpl<decltype(*this), Components...>(*this, policy, version, std::forward<Func>(func));
			}

			template <class Self, typename... Components, typename Func>
			static void forEachPolicyCommandImpl(Self &self, const ExecutionPolicy &policy, CommandBuffer & /*cmds*/, Func &&func)
			{
				constexpr ComponentMask requiredRegular{build_required_mask<Components...>()};
				const auto &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

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

				// --- Sequential execution ---
				if (policy == ExecutionPolicy::Seq)
				{
					for (Archetype *arch : matchingArchetypes)
					{
						const ui chunkCount{arch->getChunkCount()};

						for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
						{
							if (arch->getEntityCount(chunkIndex) == 0)
							{
								continue;
							}

							processChunk(arch, chunkIndex);
						}
					}
				}
				// --- Parallel (one task per chunk) ---
				else if (policy == ExecutionPolicy::Par)
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

							futures.push_back(
								self.mThreadPool.submit([arch, chunkIndex, &processChunk]() { processChunk(arch, chunkIndex); }));
						}
					}

					for (auto &fut : futures)
					{
						fut.get();
					}
				}
				// --- Parallel batched (adaptive batch size) ---
				else if (policy == ExecutionPolicy::ParBatched)
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

					const std::size_t numThreads{std::thread::hardware_concurrency()};
					const std::size_t targetTasks{numThreads * 4};

					std::size_t batchSize{(allChunks.size() + targetTasks - 1) / targetTasks};
					batchSize = std::max<std::size_t>(batchSize, 1);

					std::latch latch(static_cast<std::ptrdiff_t>((allChunks.size() + batchSize - 1) / batchSize));

					for (std::size_t i{0}; i < allChunks.size(); i += batchSize)
					{
						const std::size_t end{std::min(i + batchSize, allChunks.size())};
						const std::vector<std::pair<Archetype *, ui>> batch(allChunks.begin() + static_cast<std::ptrdiff_t>(i),
																			allChunks.begin() + static_cast<std::ptrdiff_t>(end));

						self.mThreadPool.submit_with_latch(
							[batch, &processChunk]() {
								for (const auto &[arch, chunkIndex] : batch)
								{
									processChunk(arch, chunkIndex);
								}
							},
							latch);
					}
					latch.wait();
				}
				// --- Work‑stealing (fixed batch size 8) ---
				else if (policy == ExecutionPolicy::ParStealing)
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

					const std::size_t batchSize{8}; // same as in forEachPolicyImpl
					std::latch latch(static_cast<std::ptrdiff_t>((allChunks.size() + batchSize - 1) / batchSize));

					self.mWorkStealingPool.submit_chunks(
						allChunks, [&processChunk](Archetype *arch, ui chunkIndex) { processChunk(arch, chunkIndex); }, latch, batchSize);

					latch.wait();
				}
			}

			template <typename... Components, typename Func>
			void forEach(const ExecutionPolicy &policy, CommandBuffer &cmds, Func &&func)
			{
				forEachPolicyCommandImpl<decltype(*this), Components...>(*this, policy, cmds, std::forward<Func>(func));
			}

			template <typename... Components, typename Func>
			void forEach(const ExecutionPolicy &policy, CommandBuffer &cmds, Func &&func) const
			{
				forEachPolicyCommandImpl<decltype(*this), Components...>(*this, policy, cmds, std::forward<Func>(func));
			}

			template <class Self, typename... Components, typename Func>
			static void forEachPolicyVersionCommandImpl(Self &self, const ExecutionPolicy &policy, SystemVersion &version,
														CommandBuffer &cmds, Func &&func)
			{
				constexpr ComponentMask requiredRegular{build_required_mask<Components...>()};
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

				// --- Sequential ---
				if (policy == ExecutionPolicy::Seq)
				{
					for (const auto &chunk : dirtyChunks)
					{
						processChunk(std::get<0>(chunk), std::get<1>(chunk));
						updateVersion(chunk);
					}
				}
				// --- Parallel (one task per chunk) ---
				else if (policy == ExecutionPolicy::Par)
				{
					std::vector<std::future<void>> futures;
					futures.reserve(dirtyChunks.size());

					for (const auto &chunk : dirtyChunks)
					{
						Archetype *arch{std::get<0>(chunk)};
						ui chunkIndex{std::get<1>(chunk)};

						futures.push_back(self.mThreadPool.submit([arch, chunkIndex, &processChunk]() { processChunk(arch, chunkIndex); }));
					}

					for (auto &fut : futures)
					{
						fut.get();
					}

					for (const auto &chunk : dirtyChunks)
					{
						updateVersion(chunk);
					}
				}
				// --- Parallel batched ---
				else if (policy == ExecutionPolicy::ParBatched)
				{
					// Convert to simple pairs for batching
					std::vector<std::pair<Archetype *, ui>> chunks;
					chunks.reserve(dirtyChunks.size());

					for (const auto &chunk : dirtyChunks)
					{
						chunks.emplace_back(std::get<0>(chunk), std::get<1>(chunk));
					}

					const std::size_t numThreads{std::thread::hardware_concurrency()};
					const std::size_t targetTasks{numThreads * 4};
					std::size_t batchSize{(chunks.size() + targetTasks - 1) / targetTasks};
					batchSize = std::max<std::size_t>(batchSize, 1);

					std::latch latch(static_cast<std::ptrdiff_t>((chunks.size() + batchSize - 1) / batchSize));

					for (std::size_t i{0}; i < chunks.size(); i += batchSize)
					{
						const std::size_t end{std::min(i + batchSize, chunks.size())};
						const std::vector<std::pair<Archetype *, ui>> batch(chunks.begin() + static_cast<std::ptrdiff_t>(i),
																			chunks.begin() + static_cast<std::ptrdiff_t>(end));

						self.mThreadPool.submit_with_latch(
							[batch, &processChunk]() {
								for (const auto &[arch, chunkIndex] : batch)
								{
									processChunk(arch, chunkIndex);
								}
							},
							latch);
					}

					latch.wait();

					for (const auto &chunk : dirtyChunks)
					{
						updateVersion(chunk);
					}
				}
				// --- Work‑stealing ---
				else if (policy == ExecutionPolicy::ParStealing)
				{
					std::vector<std::pair<Archetype *, ui>> chunks;
					chunks.reserve(dirtyChunks.size());

					for (const auto &chunk : dirtyChunks)
					{
						chunks.emplace_back(std::get<0>(chunk), std::get<1>(chunk));
					}

					const std::size_t batchSize{8}; // same as in forEachPolicyImpl
					std::latch latch(static_cast<std::ptrdiff_t>((chunks.size() + batchSize - 1) / batchSize));

					self.mWorkStealingPool.submit_chunks(
						chunks, [&processChunk](Archetype *arch, const ui chunkIndex) { processChunk(arch, chunkIndex); }, latch,
						batchSize);

					latch.wait();

					for (const auto &chunk : dirtyChunks)
					{
						updateVersion(chunk);
					}
				}
			}

			template <typename... Components, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
			{
				forEachPolicyVersionCommandImpl<decltype(*this), Components...>(*this, policy, version, cmds, std::forward<Func>(func));
			}

			template <typename... Components, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
			{
				forEachPolicyVersionCommandImpl<decltype(*this), Components...>(*this, policy, version, cmds, std::forward<Func>(func));
			}

		private:
			// MARK: Private Getters

			void *getComponentPtr(const Entity &entity, const ComponentTypeID compID);
			const void *getComponentPtr(const Entity &entity, const ComponentTypeID compID) const;
			Archetype *getOrCreateArchetype(ComponentMask regularMask);

			// MARK: Private Member Functions
			
			void moveEntity(const Entity &entity, ComponentMask newRegularMask, const std::array<const void *, MAX_COMPONENTS> &copyData,
							const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask newTags = ComponentMask(0));
			void destroyHierarchy(const Entity &entity);

			friend class CommandBuffer;

		private:
			QueryCache mQueryCache{};

			// NOLINTBEGIN(readability-redundant-member-init)
			mutable ThreadPool mThreadPool{};
			mutable WorkStealingPool mWorkStealingPool{};

			mutable std::mutex mHierarchyMutex{};

			std::unordered_map<ComponentMask, ui> mArchetypeMaskToID{};
			// NOLINTEND(readability-redundant-member-init)

			std::vector<EntityRecord> mRecords;
			std::vector<ui> mFreeIndices;
			std::vector<std::unique_ptr<Archetype>> mArchetypePtrs;
			std::vector<Entity> mParent;
			std::vector<std::vector<Entity>> mChildren;

			ui mNextEntityIndex{0};
	};
} // namespace Dimensia::ECS

#endif