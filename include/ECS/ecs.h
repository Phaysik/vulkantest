/*! \file ecs.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_ECS_H
#define INCLUDE_ECS_ECS_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

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
	using Registry::ComponentTypeId;
	using Registry::MAX_COMPONENTS;

	using Threading::Latch;
	using Threading::ThreadPool;
	using Threading::WorkStealingPool;

	enum class ExecutionPolicy
	{
		Seq,
		Par,
		ParBatched,
		ParStealing
	};

	class ECS
	{
		public:
			ECS();
			~ECS() = default;

			// Entity management
			Entity createEntity();
			void destroyEntity(Entity entity, const bool destroyChildren = true);
			bool alive(Entity entity) const;

			template <typename... Ts>
			Entity createEntityWith(Ts &&...components)
			{
				std::array<ComponentTypeId, sizeof...(Ts)> compIds{componentId<std::decay_t<Ts>>()...};
				ComponentMask regularMask{0, 0}, tagMask{0, 0};
				std::array<const void *, MAX_COMPONENTS> copyData{};
				std::array<void *, MAX_COMPONENTS> moveData{};
				copyData.fill(nullptr);
				moveData.fill(nullptr);

				[&]<std::size_t... I>(std::index_sequence<I...>) {
					(([&] {
						 ComponentTypeId id = compIds[I];
						 const auto &info = ComponentInfos[id];
						 if (info.isTag)
						 {
							 if (id < 64)
							 {
								 tagMask.low |= (uint64_t(1) << id);
							 }
							 else
							 {
								 tagMask.high |= (uint64_t(1) << (id - 64));
							 }
						 }
						 else
						 {
							 if (id < 64)
							 {
								 regularMask.low |= (uint64_t(1) << id);
							 }
							 else
							 {
								 regularMask.high |= (uint64_t(1) << (id - 64));
							 }
							 if constexpr (std::is_const_v<std::remove_reference_t<decltype(components)>>)
							 {
								 copyData[id] = &components;
							 }
							 else
							 {
								 moveData[id] = &components;
							 }
						 }
					 }()),
					 ...);
				}(std::index_sequence_for<Ts...>{});

				Entity e = createEntity();
				if (regularMask || tagMask)
				{
					if (regularMask)
					{
						moveEntity(e, regularMask, copyData, moveData, tagMask);
					}
					else
					{
						moveEntity(e, ComponentMask(0), copyData, moveData, tagMask);
					}
				}
				return e;
			}

			// Component management
			template <typename T>
			void addComponent(Entity entity, T value)
			{
				if (!alive(entity))
				{
					return;
				}
				ComponentTypeId compId = componentId<T>();
				const auto &info = ComponentInfos[compId];
				if (info.isTag)
				{
					auto &rec = records_[entity.index];
					archetypePtrs_[rec.archetypeId]->setTag(rec.chunkIndex, rec.slotIndex, compId);
					return;
				}

				Archetype *arch = archetypePtrs_[records_[entity.index].archetypeId].get();
				ComponentMask oldRegular = arch->getRegularMask();
				ComponentMask newRegular = oldRegular;
				if (compId < 64)
				{
					newRegular.low |= (uint64_t(1) << compId);
				}
				else
				{
					newRegular.high |= (uint64_t(1) << (compId - 64));
				}

				if (oldRegular == newRegular)
				{
					T *ptr = static_cast<T *>(getComponentPtr(entity, compId));
					*ptr = std::move(value);
					archetypePtrs_[records_[entity.index].archetypeId]->bumpComponentVersion(records_[entity.index].chunkIndex, compId);
					return;
				}

				std::array<const void *, MAX_COMPONENTS> copyData{};
				std::array<void *, MAX_COMPONENTS> moveData{};
				moveData[compId] = &value;
				moveEntity(entity, newRegular, copyData, moveData);
			}

			template <typename T>
			void removeComponent(Entity entity)
			{
				if (!alive(entity))
				{
					return;
				}
				ComponentTypeId compId = componentId<T>();
				const auto &info = ComponentInfos[compId];
				if (info.isTag)
				{
					auto &rec = records_[entity.index];
					archetypePtrs_[rec.archetypeId]->clearTag(rec.chunkIndex, rec.slotIndex, compId);
					return;
				}
				removeComponent(entity, compId); // non‑template version already updated
			}

			void removeComponent(Entity entity, ComponentTypeId compId); // non‑template

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
				ComponentTypeId tagId = componentId<Tag>();
				if (!ComponentInfos[tagId].isTag)
				{
					return;
				}
				auto &rec = records_[entity.index];
				archetypePtrs_[rec.archetypeId]->setTag(rec.chunkIndex, rec.slotIndex, tagId);
			}

			template <typename Tag>
			void removeTag(Entity entity)
			{
				if (!alive(entity))
				{
					return;
				}
				ComponentTypeId tagId = componentId<Tag>();
				if (!ComponentInfos[tagId].isTag)
				{
					return;
				}
				auto &rec = records_[entity.index];
				archetypePtrs_[rec.archetypeId]->clearTag(rec.chunkIndex, rec.slotIndex, tagId);
			}

			template <typename Tag>
			bool hasTag(Entity entity) const
			{
				if (!alive(entity))
				{
					return false;
				}
				ComponentTypeId tagId = componentId<Tag>();
				if (!ComponentInfos[tagId].isTag)
				{
					return false;
				}
				const auto &rec = records_[entity.index];
				return archetypePtrs_[rec.archetypeId]->hasTag(rec.chunkIndex, rec.slotIndex, tagId);
			}

			// Hierarchy
			void setParent(Entity child, Entity parent);
			std::vector<Entity> getChildren(Entity parent) const;
			Entity getParent(Entity child) const;

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

			template <typename... Components, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func)
			{
				constexpr ComponentMask requiredRegular = build_required_mask<Components...>();
				const auto &matchingArchetypes = queryCache_.get(requiredRegular);

				if (policy == ExecutionPolicy::Seq)
				{
					for (Archetype *arch : matchingArchetypes)
					{
						uint32_t chunkCount = arch->getChunkCount();
						for (uint32_t c = 0; c < chunkCount; ++c)
						{
							uint32_t entityCount = arch->getEntityCount(c);
							if (entityCount == 0)
							{
								continue;
							}
							Entity *entityArr = arch->getEntityArray(c);
							auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
							process_chunk_entities<Components...>(entityArr, compArrays, entityCount, c, arch, func);
						}
					}
				}
				else if (policy == ExecutionPolicy::Par)
				{
					std::vector<std::future<void>> futures;
					futures.reserve(matchingArchetypes.size() * 2);
					for (Archetype *arch : matchingArchetypes)
					{
						uint32_t chunkCount = arch->getChunkCount();
						for (uint32_t c = 0; c < chunkCount; ++c)
						{
							uint32_t entityCount = arch->getEntityCount(c);
							if (entityCount == 0)
							{
								continue;
							}
							futures.push_back(threadPool_.submit([arch, c, entityCount, func]() {
								Entity *entityArr = arch->getEntityArray(c);
								auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
								process_chunk_entities<Components...>(entityArr, compArrays, entityCount, c, arch, func);
							}));
						}
					}
					for (auto &fut : futures)
					{
						fut.get();
					}
				}
				else if (policy == ExecutionPolicy::ParBatched)
				{
					std::vector<std::pair<Archetype *, uint32_t>> allChunks;
					for (Archetype *arch : matchingArchetypes)
					{
						uint32_t chunkCount = arch->getChunkCount();
						for (uint32_t c = 0; c < chunkCount; ++c)
						{
							if (arch->getEntityCount(c) > 0)
							{
								allChunks.emplace_back(arch, c);
							}
						}
					}
					if (allChunks.empty())
					{
						return;
					}
					Latch latch(static_cast<int>((allChunks.size() + 3) / 4)); // batch size 4
					const size_t batchSize = 4;
					for (size_t i = 0; i < allChunks.size(); i += batchSize)
					{
						size_t end = std::min(i + batchSize, allChunks.size());
						std::vector<std::pair<Archetype *, uint32_t>> batch(allChunks.begin() + static_cast<std::ptrdiff_t>(i),
																			allChunks.begin() + static_cast<std::ptrdiff_t>(end));
						threadPool_.submit_with_latch(
							[batch, func]() {
								for (const auto &[arch, c] : batch)
								{
									uint32_t entityCount = arch->getEntityCount(c);
									Entity *entityArr = arch->getEntityArray(c);
									auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
									process_chunk_entities<Components...>(entityArr, compArrays, entityCount, c, arch, func);
								}
							},
							latch);
					}
					latch.wait();
				}
				else if (policy == ExecutionPolicy::ParStealing)
				{
					std::vector<std::pair<Archetype *, uint32_t>> allChunks;
					for (Archetype *arch : matchingArchetypes)
					{
						uint32_t chunkCount = arch->getChunkCount();
						for (uint32_t c = 0; c < chunkCount; ++c)
						{
							if (arch->getEntityCount(c) > 0)
							{
								allChunks.emplace_back(arch, c);
							}
						}
					}
					if (allChunks.empty())
					{
						return;
					}
					Latch latch(static_cast<int>((allChunks.size() + 7) / 8)); // batch size 8
					workStealingPool_.submit_chunks(
						allChunks,
						[func](Archetype *arch, uint32_t c) {
							uint32_t entityCount = arch->getEntityCount(c);
							Entity *entityArr = arch->getEntityArray(c);
							auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
							process_chunk_entities<Components...>(entityArr, compArrays, entityCount, c, arch, func);
						},
						latch, 8);
					latch.wait();
				}
			}

			template <typename... Components, typename Func>
			void forEach(ExecutionPolicy policy, Func &&func) const
			{
				constexpr ComponentMask requiredRegular = build_required_mask<Components...>();
				const auto &matchingArchetypes = queryCache_.get(requiredRegular);

				if (policy == ExecutionPolicy::Seq)
				{
					for (Archetype *arch : matchingArchetypes)
					{
						uint32_t chunkCount = arch->getChunkCount();
						for (uint32_t c = 0; c < chunkCount; ++c)
						{
							uint32_t entityCount = arch->getEntityCount(c);
							if (entityCount == 0)
							{
								continue;
							}
							const Entity *entityArr = arch->getEntityArray(c);
							auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
							process_chunk_entities_const<Components...>(entityArr, compArrays, entityCount, c, arch, func);
						}
					}
				}
				else if (policy == ExecutionPolicy::Par)
				{
					std::vector<std::future<void>> futures;
					futures.reserve(matchingArchetypes.size() * 2);
					for (Archetype *arch : matchingArchetypes)
					{
						uint32_t chunkCount = arch->getChunkCount();
						for (uint32_t c = 0; c < chunkCount; ++c)
						{
							uint32_t entityCount = arch->getEntityCount(c);
							if (entityCount == 0)
							{
								continue;
							}
							futures.push_back(threadPool_.submit([arch, c, entityCount, func]() {
								const Entity *entityArr = arch->getEntityArray(c);
								auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
								process_chunk_entities_const<Components...>(entityArr, compArrays, entityCount, c, arch, func);
							}));
						}
					}
					for (auto &fut : futures)
					{
						fut.get();
					}
				}
				else if (policy == ExecutionPolicy::ParBatched)
				{
					std::vector<std::pair<Archetype *, uint32_t>> allChunks;
					for (Archetype *arch : matchingArchetypes)
					{
						uint32_t chunkCount = arch->getChunkCount();
						for (uint32_t c = 0; c < chunkCount; ++c)
						{
							if (arch->getEntityCount(c) > 0)
							{
								allChunks.emplace_back(arch, c);
							}
						}
					}
					if (allChunks.empty())
					{
						return;
					}
					Latch latch(static_cast<int>((allChunks.size() + 3) / 4));
					const size_t batchSize = 4;
					for (size_t i = 0; i < allChunks.size(); i += batchSize)
					{
						size_t end = std::min(i + batchSize, allChunks.size());
						std::vector<std::pair<Archetype *, uint32_t>> batch(allChunks.begin() + static_cast<std::ptrdiff_t>(i),
																			allChunks.begin() + static_cast<std::ptrdiff_t>(end));
						threadPool_.submit_with_latch(
							[batch, func]() {
								for (const auto &[arch, c] : batch)
								{
									uint32_t entityCount = arch->getEntityCount(c);
									const Entity *entityArr = arch->getEntityArray(c);
									auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
									process_chunk_entities_const<Components...>(entityArr, compArrays, entityCount, c, arch, func);
								}
							},
							latch);
					}
					latch.wait();
				}
				else if (policy == ExecutionPolicy::ParStealing)
				{
					std::vector<std::pair<Archetype *, uint32_t>> allChunks;
					for (Archetype *arch : matchingArchetypes)
					{
						uint32_t chunkCount = arch->getChunkCount();
						for (uint32_t c = 0; c < chunkCount; ++c)
						{
							if (arch->getEntityCount(c) > 0)
							{
								allChunks.emplace_back(arch, c);
							}
						}
					}
					if (allChunks.empty())
					{
						return;
					}
					Latch latch(static_cast<int>((allChunks.size() + 7) / 8));
					workStealingPool_.submit_chunks(
						allChunks,
						[func](Archetype *arch, uint32_t c) {
							uint32_t entityCount = arch->getEntityCount(c);
							const Entity *entityArr = arch->getEntityArray(c);
							auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
							process_chunk_entities_const<Components...>(entityArr, compArrays, entityCount, c, arch, func);
						},
						latch, 8);
					latch.wait();
				}
			}

			template <typename... Components, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
			{
				constexpr ComponentMask requiredRegular = build_required_mask<Components...>();
				const auto &matchingArchetypes = queryCache_.get(requiredRegular);

				std::vector<std::tuple<Archetype *, uint32_t, const ChunkVersion *>> dirtyChunks;
				for (Archetype *arch : matchingArchetypes)
				{
					uint32_t chunkCount = arch->getChunkCount();
					for (uint32_t c = 0; c < chunkCount; ++c)
					{
						uint32_t entityCount = arch->getEntityCount(c);
						if (entityCount == 0)
						{
							continue;
						}
						const auto &chunkVersion = arch->getChunkVersion(c);
						if (version.needsUpdate(chunkVersion, requiredRegular))
						{
							dirtyChunks.emplace_back(arch, c, &chunkVersion);
						}
					}
				}
				if (dirtyChunks.empty())
				{
					return;
				}

				auto processFunc = [&](Archetype *arch, uint32_t c) {
					uint32_t entityCount = arch->getEntityCount(c);
					Entity *entityArr = arch->getEntityArray(c);
					auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
					process_chunk_entities<Components...>(entityArr, compArrays, entityCount, c, arch, func);
				};

				if (policy == ExecutionPolicy::Seq)
				{
					for (const auto &chunk : dirtyChunks)
					{
						Archetype *arch = std::get<0>(chunk);
						uint32_t c = std::get<1>(chunk);
						const ChunkVersion *chunkVer = std::get<2>(chunk);
						processFunc(arch, c);
						forEachSetBit(requiredRegular,
									  [&](ComponentTypeId id) { version.componentVersions[id] = chunkVer->componentVersions[id]; });
					}
					const auto &lastChunk = dirtyChunks.back();
					version.version = std::max(version.version, std::get<0>(lastChunk)->getChunkVersion(std::get<1>(lastChunk)).version);
				}
				else if (policy == ExecutionPolicy::Par)
				{
					std::vector<std::future<void>> futures;
					futures.reserve(dirtyChunks.size());
					for (const auto &chunk : dirtyChunks)
					{
						Archetype *arch = std::get<0>(chunk);
						uint32_t c = std::get<1>(chunk);
						futures.push_back(threadPool_.submit([arch, c, processFunc]() { processFunc(arch, c); }));
					}
					for (auto &fut : futures)
					{
						fut.get();
					}
					for (const auto &chunk : dirtyChunks)
					{
						Archetype *arch = std::get<0>(chunk);
						uint32_t c = std::get<1>(chunk);
						const ChunkVersion *chunkVer = std::get<2>(chunk);
						forEachSetBit(requiredRegular, [&](ComponentTypeId id) {
							version.componentVersions[id] = std::max(version.componentVersions[id], chunkVer->componentVersions[id]);
						});
						version.version = std::max(version.version, arch->getChunkVersion(c).version);
					}
				}
				else if (policy == ExecutionPolicy::ParBatched || policy == ExecutionPolicy::ParStealing)
				{
					std::vector<std::pair<Archetype *, uint32_t>> chunks;
					for (const auto &chunk : dirtyChunks)
					{
						chunks.emplace_back(std::get<0>(chunk), std::get<1>(chunk));
					}
					Latch latch(static_cast<int>((chunks.size() + 3) / 4));
					const size_t batchSize = 4;
					for (size_t i = 0; i < chunks.size(); i += batchSize)
					{
						size_t end = std::min(i + batchSize, chunks.size());
						std::vector<std::pair<Archetype *, uint32_t>> batch(chunks.begin() + static_cast<std::ptrdiff_t>(i),
																			chunks.begin() + static_cast<std::ptrdiff_t>(end));
						threadPool_.submit_with_latch(
							[batch, processFunc]() {
								for (const auto &[arch, c] : batch)
								{
									processFunc(arch, c);
								}
							},
							latch);
					}
					latch.wait();
					for (const auto &chunk : dirtyChunks)
					{
						Archetype *arch = std::get<0>(chunk);
						uint32_t c = std::get<1>(chunk);
						const ChunkVersion *chunkVer = std::get<2>(chunk);
						forEachSetBit(requiredRegular, [&](ComponentTypeId id) {
							version.componentVersions[id] = std::max(version.componentVersions[id], chunkVer->componentVersions[id]);
						});
						version.version = std::max(version.version, arch->getChunkVersion(c).version);
					}
				}
			}

			template <typename... Components, typename Func>
			void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
			{
				constexpr ComponentMask requiredRegular = build_required_mask<Components...>();
				const auto &matchingArchetypes = queryCache_.get(requiredRegular);

				std::vector<std::tuple<Archetype *, uint32_t, const ChunkVersion *>> dirtyChunks;
				for (Archetype *arch : matchingArchetypes)
				{
					uint32_t chunkCount = arch->getChunkCount();
					for (uint32_t c = 0; c < chunkCount; ++c)
					{
						uint32_t entityCount = arch->getEntityCount(c);
						if (entityCount == 0)
						{
							continue;
						}
						const auto &chunkVersion = arch->getChunkVersion(c);
						if (version.needsUpdate(chunkVersion, requiredRegular))
						{
							dirtyChunks.emplace_back(arch, c, &chunkVersion);
						}
					}
				}
				if (dirtyChunks.empty())
				{
					return;
				}

				auto processFunc = [&](Archetype *arch, uint32_t c) {
					uint32_t entityCount = arch->getEntityCount(c);
					const Entity *entityArr = arch->getEntityArray(c);
					auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
					process_chunk_entities_const<Components...>(entityArr, compArrays, entityCount, c, arch, func);
				};

				// For simplicity, const version only implements sequential.
				if (policy == ExecutionPolicy::Seq)
				{
					for (const auto &chunk : dirtyChunks)
					{
						Archetype *arch = std::get<0>(chunk);
						uint32_t c = std::get<1>(chunk);
						const ChunkVersion *chunkVer = std::get<2>(chunk);
						processFunc(arch, c);
						forEachSetBit(requiredRegular,
									  [&](ComponentTypeId id) { version.componentVersions[id] = chunkVer->componentVersions[id]; });
					}
					const auto &lastChunk = dirtyChunks.back();
					version.version = std::max(version.version, std::get<0>(lastChunk)->getChunkVersion(std::get<1>(lastChunk)).version);
				}
				else
				{
					// fallback to sequential for other policies (or could implement parallel similarly)
					for (const auto &chunk : dirtyChunks)
					{
						Archetype *arch = std::get<0>(chunk);
						uint32_t c = std::get<1>(chunk);
						processFunc(arch, c);
					}
					for (const auto &chunk : dirtyChunks)
					{
						Archetype *arch = std::get<0>(chunk);
						uint32_t c = std::get<1>(chunk);
						const ChunkVersion *chunkVer = std::get<2>(chunk);
						forEachSetBit(requiredRegular, [&](ComponentTypeId id) {
							version.componentVersions[id] = std::max(version.componentVersions[id], chunkVer->componentVersions[id]);
						});
						version.version = std::max(version.version, arch->getChunkVersion(c).version);
					}
				}
			}

			template <typename... Components, typename Func>
			void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
			{
				constexpr ComponentMask requiredRegular = build_required_mask<Components...>();
				const auto &matchingArchetypes = queryCache_.get(requiredRegular);

				if (policy == ExecutionPolicy::Seq)
				{
					for (Archetype *arch : matchingArchetypes)
					{
						uint32_t chunkCount = arch->getChunkCount();
						for (uint32_t c = 0; c < chunkCount; ++c)
						{
							uint32_t entityCount = arch->getEntityCount(c);
							if (entityCount == 0)
							{
								continue;
							}
							Entity *entityArr = arch->getEntityArray(c);
							auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
							process_chunk_entities<Components...>(entityArr, compArrays, entityCount, c, arch,
																  [&](Entity e, auto &...comps) { func(e, comps...); });
						}
					}
				}
				else if (policy == ExecutionPolicy::Par)
				{
					std::vector<std::future<void>> futures;
					futures.reserve(matchingArchetypes.size() * 2);
					for (Archetype *arch : matchingArchetypes)
					{
						uint32_t chunkCount = arch->getChunkCount();
						for (uint32_t c = 0; c < chunkCount; ++c)
						{
							uint32_t entityCount = arch->getEntityCount(c);
							if (entityCount == 0)
							{
								continue;
							}
							futures.push_back(threadPool_.submit([arch, c, entityCount, &cmds, func]() {
								Entity *entityArr = arch->getEntityArray(c);
								auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
								process_chunk_entities<Components...>(entityArr, compArrays, entityCount, c, arch,
																	  [&](Entity e, auto &...comps) { func(e, comps...); });
							}));
						}
					}
					for (auto &fut : futures)
					{
						fut.get();
					}
				}
				else if (policy == ExecutionPolicy::ParBatched)
				{
					std::vector<std::pair<Archetype *, uint32_t>> allChunks;
					for (Archetype *arch : matchingArchetypes)
					{
						uint32_t chunkCount = arch->getChunkCount();
						for (uint32_t c = 0; c < chunkCount; ++c)
						{
							if (arch->getEntityCount(c) > 0)
							{
								allChunks.emplace_back(arch, c);
							}
						}
					}
					if (allChunks.empty())
					{
						return;
					}
					Latch latch(static_cast<int>((allChunks.size() + 3) / 4));
					const size_t batchSize = 4;
					for (size_t i = 0; i < allChunks.size(); i += batchSize)
					{
						size_t end = std::min(i + batchSize, allChunks.size());
						std::vector<std::pair<Archetype *, uint32_t>> batch(allChunks.begin() + static_cast<std::ptrdiff_t>(i),
																			allChunks.begin() + static_cast<std::ptrdiff_t>(end));
						threadPool_.submit_with_latch(
							[batch, &cmds, func]() {
								for (const auto &[arch, c] : batch)
								{
									uint32_t entityCount = arch->getEntityCount(c);
									Entity *entityArr = arch->getEntityArray(c);
									auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
									process_chunk_entities<Components...>(entityArr, compArrays, entityCount, c, arch,
																		  [&](Entity e, auto &...comps) { func(e, comps...); });
								}
							},
							latch);
					}
					latch.wait();
				}
				else if (policy == ExecutionPolicy::ParStealing)
				{
					std::vector<std::pair<Archetype *, uint32_t>> allChunks;
					for (Archetype *arch : matchingArchetypes)
					{
						uint32_t chunkCount = arch->getChunkCount();
						for (uint32_t c = 0; c < chunkCount; ++c)
						{
							if (arch->getEntityCount(c) > 0)
							{
								allChunks.emplace_back(arch, c);
							}
						}
					}
					if (allChunks.empty())
					{
						return;
					}
					Latch latch(static_cast<int>((allChunks.size() + 7) / 8));
					workStealingPool_.submit_chunks(
						allChunks,
						[&cmds, func](Archetype *arch, uint32_t c) {
							uint32_t entityCount = arch->getEntityCount(c);
							Entity *entityArr = arch->getEntityArray(c);
							auto compArrays = std::make_tuple(arch->getComponentArray(c, componentId<Components>())...);
							process_chunk_entities<Components...>(entityArr, compArrays, entityCount, c, arch,
																  [&](Entity e, auto &...comps) { func(e, comps...); });
						},
						latch, 8);
					latch.wait();
				}
			}

			void compact();

		private:
			std::vector<EntityRecord> records_;
			std::vector<uint32_t> freeIndices_;
			uint32_t nextEntityIndex_ = 0;
			// Archetype storage: stable IDs via vector, and a map from mask to ID.
			std::vector<std::unique_ptr<Archetype>> archetypePtrs_;
			std::unordered_map<ComponentMask, uint32_t> archetypeMaskToId_;
			QueryCache queryCache_;
			mutable ThreadPool threadPool_;
			mutable WorkStealingPool workStealingPool_;

			mutable std::mutex hierarchyMutex_;
			std::vector<Entity> parent_;
			std::vector<std::vector<Entity>> children_;

			// Helpers
			Archetype *getOrCreateArchetype(ComponentMask regularMask);
			void moveEntity(Entity entity, ComponentMask newRegularMask, const std::array<const void *, MAX_COMPONENTS> &copyData,
							const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask newTags = ComponentMask(0));
			void *getComponentPtr(Entity entity, ComponentTypeId compId);
			const void *getComponentPtr(Entity entity, ComponentTypeId compId) const;
			void destroyHierarchy(Entity entity);

			friend class CommandBuffer;
	};
} // namespace Dimensia::ECS

#endif