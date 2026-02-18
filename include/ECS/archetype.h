/*! \file archetype.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_ARCHETYPE_H
#define INCLUDE_ECS_ARCHETYPE_H

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "Core/attributeMacros.h"
#include "Core/typedefs.h"

#include "chunkVersion.h"
#include "componentMask.h"
#include "componentRegistry.h"
#include "entity.h"
#include "entityRecord.h"

namespace Dimensia::ECS
{
	using Dimensia::Core::ui;
	using Dimensia::Core::ul;

	using Registry::ComponentTypeID;
	using Registry::MAX_COMPONENTS;

	class Archetype
	{

		public:
			explicit Archetype(ComponentMask regularMask, uint32_t id);
			~Archetype();

			Archetype(const Archetype &) = delete;
			Archetype &operator=(const Archetype &) = delete;

			std::pair<ui, ui> addEntity(Entity entity, const std::array<const void *, MAX_COMPONENTS> &copyData,
										const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask tags = ComponentMask(0));
			std::pair<Entity, ui> removeEntity(ui chunkIdx, ui slotIdx);

			void compact(std::vector<EntityRecord> &globalRecords);

			// Getters
			ATTR_NODISCARD ComponentMask getRegularMask() const;

			ATTR_NODISCARD ui getChunkCount() const;

			ATTR_NODISCARD const ChunkVersion &getChunkVersion(ui chunkIdx) const;

			ATTR_NODISCARD ui getId() const;

			ATTR_NODISCARD ui getEntityCount(ui chunkIdx) const;
			ATTR_NODISCARD void *getComponentArray(ui chunkIdx, ComponentTypeID compId) const;
			ATTR_NODISCARD Entity *getEntityArray(ui chunkIdx) const;
			ATTR_NODISCARD bool hasTag(ui chunkIdx, ui slotIdx, ComponentTypeID tagId) const;
			void setTag(ui chunkIdx, ui slotIdx, ComponentTypeID tagId);
			void clearTag(ui chunkIdx, ui slotIdx, ComponentTypeID tagId);
			ATTR_NODISCARD ComponentMask getTags(ui chunkIdx, ui slotIdx) const;

			void bumpChunkVersion(ui chunkIdx);

			void bumpComponentVersion(ui chunkIdx, ComponentTypeID compId);

			template <typename F>
			void forEachComponent(F &&func) const
			{
				forEachSetBit(regularMask_, std::forward<F>(func));
			}

			const ul *getTagBitset(ui chunkIdx) const
			{
				return reinterpret_cast<const ul *>(chunks_[chunkIdx]->buffer + tagBitsetOffset_);
			}

		private:
			static constexpr std::size_t CHUNK_SIZE = 16'384;

			struct Chunk
			{
					alignas(64) std::byte buffer[CHUNK_SIZE];
					ui count = 0;
					ui capacity = 0;
			};

			ComponentMask regularMask_;
			uint32_t archetypeId_; // stable ID assigned by ECS
			std::vector<std::unique_ptr<Chunk>> chunks_;
			std::vector<ui> freeChunks_;
			ui chunkCapacity_;
			std::array<std::size_t, MAX_COMPONENTS> componentOffsets_{};
			std::array<std::size_t, MAX_COMPONENTS> componentSizes_{};
			std::size_t entityArrayOffset_{0};
			std::size_t tagBitsetOffset_{0};
			std::vector<ComponentTypeID> sortedRegular_;
			std::vector<ChunkVersion> chunkVersions_;

			ATTR_NODISCARD ui computeCapacity() const;
			void computeLayout(ui capacity);

			ul *getTagBitset(Chunk *chunk) const;
			const ul *getTagBitset(const Chunk *chunk) const;
	};
} // namespace Dimensia::ECS

#endif