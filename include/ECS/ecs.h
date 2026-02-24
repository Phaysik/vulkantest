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

			static std::size_t getBatchSize(const std::size_t allChunkSize) noexcept
			{
				// Adaptive batch size based on hardware concurrency
				const std::size_t numThreads{std::thread::hardware_concurrency()};
				const std::size_t targetTasks{numThreads * 4};

				const std::size_t batchSize{(allChunkSize + targetTasks - 1) / targetTasks};

				return std::max<std::size_t>(batchSize, 1);
			}

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

			// MARK: forEach Template Member Functions

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

			// MARK: forEachPolicyImpl

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

			// MARK: forEachPolicyCommandImpl

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

			// MARK: forEachPolicyVersionImpl

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

			// MARK: forEachPolicyVersionCommandImpl

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