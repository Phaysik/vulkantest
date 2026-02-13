#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <tuple> // for std::tuple, std::get, std::apply, std::make_tuple
#include <unordered_map>
#include <utility> // for std::index_sequence, etc.
#include <vector>

// -----------------------------------------------------------------------------
//  Configuration
// -----------------------------------------------------------------------------
constexpr size_t CHUNK_SIZE = 16'384;
constexpr size_t MAX_COMPONENTS = 64;
using ComponentMask = uint64_t;
using ComponentTypeId = uint32_t;

// -----------------------------------------------------------------------------
//  Component Registry – type erased size, alignment, destructor, copy constructor
// -----------------------------------------------------------------------------
struct ComponentInfo
{
		size_t size;
		size_t alignment;
		void (*destructor)(void *);
		void (*copyConstruct)(void *dest, const void *src);
};

namespace internal
{
	// *Declarations* first (fixes -Wmissing-declarations)
	std::unordered_map<ComponentTypeId, ComponentInfo> &getComponentRegistry();
	ComponentTypeId &nextComponentTypeId();
} // namespace internal

namespace internal
{
	std::unordered_map<ComponentTypeId, ComponentInfo> &getComponentRegistry()
	{
		static std::unordered_map<ComponentTypeId, ComponentInfo> reg;
		return reg;
	}

	ComponentTypeId &nextComponentTypeId()
	{
		static ComponentTypeId id = 0;
		return id;
	}
} // namespace internal

template <typename T>
ComponentTypeId componentId()
{
	static ComponentTypeId id = [] {
		ComponentTypeId newId = internal::nextComponentTypeId()++;
		internal::getComponentRegistry()[newId] = {sizeof(T), alignof(T), [](void *ptr) { static_cast<T *>(ptr)->~T(); },
												   [](void *dest, const void *src) { new (dest) T(*static_cast<const T *>(src)); }};
		return newId;
	}();
	return id;
}

// -----------------------------------------------------------------------------
//  Entity – generational handle
// -----------------------------------------------------------------------------
struct Entity
{
		uint32_t index;
		uint32_t generation;
		bool operator==(const Entity &other) const = default;
};

// -----------------------------------------------------------------------------
//  Forward declarations
// -----------------------------------------------------------------------------
class Archetype;

struct EntityRecord
{
		uint32_t generation;
		Archetype *archetype; // nullptr = dead
		uint32_t chunkIndex;
		uint32_t slotIndex;
};

// -----------------------------------------------------------------------------
//  Archetype – unique set of components, owns chunks
// -----------------------------------------------------------------------------
class Archetype
{
	public:
		explicit Archetype(ComponentMask mask);
		~Archetype();
		Archetype(const Archetype &) = delete;
		Archetype &operator=(const Archetype &) = delete;

		std::pair<uint32_t, uint32_t> addEntity(Entity entity, const std::unordered_map<ComponentTypeId, const void *> &componentData);

		std::pair<Entity, uint32_t> removeEntity(uint32_t chunkIdx, uint32_t slotIdx);

		ComponentMask getMask() const
		{
			return mask_;
		}

		uint32_t getChunkCount() const
		{
			return static_cast<uint32_t>(chunks_.size());
		}

		uint32_t getEntityCount(uint32_t chunkIdx) const
		{
			return chunks_[chunkIdx]->count;
		}

		void *getComponentArray(uint32_t chunkIdx, ComponentTypeId compId) const;
		Entity *getEntityArray(uint32_t chunkIdx) const;

	private:
		struct Chunk
		{
				alignas(alignof(std::max_align_t)) std::byte buffer[CHUNK_SIZE];
				uint32_t count = 0;
				uint32_t capacity = 0;
		};

		ComponentMask mask_;
		std::vector<std::unique_ptr<Chunk>> chunks_;
		std::vector<uint32_t> freeChunks_;
		uint32_t chunkCapacity_ = 0;
		size_t entityArrayOffset_ = 0;
		std::unordered_map<ComponentTypeId, size_t> componentOffsets_;
		std::unordered_map<ComponentTypeId, size_t> componentSizes_;
		std::vector<ComponentTypeId> sortedComponents_;

		uint32_t computeCapacity() const;
		void computeLayout(uint32_t capacity);
};

// -----------------------------------------------------------------------------
//  Archetype implementation
// -----------------------------------------------------------------------------
Archetype::Archetype(ComponentMask mask)
	: mask_(mask), chunks_(), freeChunks_(), chunkCapacity_(0), entityArrayOffset_(0), componentOffsets_(), componentSizes_(),
	  sortedComponents_() // explicit init avoids -Weffc++
{
	for (ComponentTypeId id = 0; id < MAX_COMPONENTS; ++id)
	{
		if (mask_ & (ComponentMask(1) << id))
		{
			sortedComponents_.push_back(id);
		}
	}
	std::sort(sortedComponents_.begin(), sortedComponents_.end());
	chunkCapacity_ = computeCapacity();
	computeLayout(chunkCapacity_);
}

Archetype::~Archetype()
{
	for (auto &chunk : chunks_)
	{
		for (uint32_t slot = 0; slot < chunk->count; ++slot)
		{
			for (ComponentTypeId compId : sortedComponents_)
			{
				void *ptr = static_cast<std::byte *>(chunk->buffer) + componentOffsets_.at(compId) + slot * componentSizes_.at(compId);
				internal::getComponentRegistry().at(compId).destructor(ptr);
			}
		}
	}
}

uint32_t Archetype::computeCapacity() const
{
	size_t perEntity = sizeof(Entity);
	for (ComponentTypeId id : sortedComponents_)
	{
		perEntity += internal::getComponentRegistry().at(id).size;
	}

	uint32_t cap = static_cast<uint32_t>(CHUNK_SIZE / perEntity) + 1;
	while (true)
	{
		size_t offset = 0;
		offset += cap * sizeof(Entity);
		for (ComponentTypeId id : sortedComponents_)
		{
			const auto &info = internal::getComponentRegistry().at(id);
			offset = (offset + info.alignment - 1) & ~(info.alignment - 1);
			offset += cap * info.size;
		}
		if (offset <= CHUNK_SIZE)
		{
			break;
		}
		--cap;
		assert(cap > 0);
	}
	return cap;
}

void Archetype::computeLayout(uint32_t capacity)
{
	size_t offset = 0;
	entityArrayOffset_ = offset;
	offset += capacity * sizeof(Entity);

	for (ComponentTypeId id : sortedComponents_)
	{
		const auto &info = internal::getComponentRegistry().at(id);
		offset = (offset + info.alignment - 1) & ~(info.alignment - 1);
		componentOffsets_[id] = offset;
		componentSizes_[id] = info.size;
		offset += capacity * info.size;
	}
	assert(offset <= CHUNK_SIZE);
}

std::pair<uint32_t, uint32_t> Archetype::addEntity(Entity entity, const std::unordered_map<ComponentTypeId, const void *> &componentData)
{
	Chunk *chunk = nullptr;
	uint32_t chunkIdx = 0;

	// 1. Try to find an existing chunk with free slots
	for (; chunkIdx < chunks_.size(); ++chunkIdx)
	{
		if (chunks_[chunkIdx]->count < chunkCapacity_)
		{
			chunk = chunks_[chunkIdx].get();
			break;
		}
	}

	// 2. If none, reuse an empty chunk from the free list
	if (!chunk && !freeChunks_.empty())
	{
		chunkIdx = freeChunks_.back();
		freeChunks_.pop_back();
		chunk = chunks_[chunkIdx].get();
		chunk->count = 0; // reset – ready for new entities
	}

	// 3. Otherwise create a brand new chunk
	if (!chunk)
	{
		auto newChunk = std::make_unique<Chunk>();
		newChunk->capacity = chunkCapacity_;
		chunk = newChunk.get();
		chunks_.push_back(std::move(newChunk));
		chunkIdx = static_cast<uint32_t>(chunks_.size() - 1);
	}

	uint32_t slot = chunk->count++;
	Entity *entityArr = reinterpret_cast<Entity *>(chunk->buffer + entityArrayOffset_);
	new (&entityArr[slot]) Entity(entity);

	for (ComponentTypeId id : sortedComponents_)
	{
		const void *src = componentData.at(id);
		size_t offset = componentOffsets_.at(id);
		size_t size = componentSizes_.at(id);
		void *dest = chunk->buffer + offset + slot * size;
		internal::getComponentRegistry().at(id).copyConstruct(dest, src);
	}
	return {chunkIdx, slot};
}

std::pair<Entity, uint32_t> Archetype::removeEntity(uint32_t chunkIdx, uint32_t slotIdx)
{
	Chunk *chunk = chunks_[chunkIdx].get();
	assert(slotIdx < chunk->count);

	uint32_t lastSlot = chunk->count - 1;
	Entity *entityArr = reinterpret_cast<Entity *>(chunk->buffer + entityArrayOffset_);

	for (ComponentTypeId id : sortedComponents_)
	{
		size_t offset = componentOffsets_.at(id);
		size_t size = componentSizes_.at(id);
		void *ptr = chunk->buffer + offset + slotIdx * size;
		internal::getComponentRegistry().at(id).destructor(ptr);
	}

	Entity movedEntity{0, 0};
	if (slotIdx != lastSlot)
	{
		movedEntity = entityArr[lastSlot];
		entityArr[slotIdx] = movedEntity;

		for (ComponentTypeId id : sortedComponents_)
		{
			size_t offset = componentOffsets_.at(id);
			size_t size = componentSizes_.at(id);
			void *dest = chunk->buffer + offset + slotIdx * size;
			void *src = chunk->buffer + offset + lastSlot * size;
			internal::getComponentRegistry().at(id).copyConstruct(dest, src);
		}
		for (ComponentTypeId id : sortedComponents_)
		{
			size_t offset = componentOffsets_.at(id);
			size_t size = componentSizes_.at(id);
			void *ptr = chunk->buffer + offset + lastSlot * size;
			internal::getComponentRegistry().at(id).destructor(ptr);
		}
	}

	--chunk->count;

	// 🔁 If chunk becomes completely empty, add its index to the free list
	if (chunk->count == 0)
	{
		freeChunks_.push_back(chunkIdx);
	}

	return {movedEntity, slotIdx};
}

void *Archetype::getComponentArray(uint32_t chunkIdx, ComponentTypeId compId) const
{
	auto it = componentOffsets_.find(compId);
	if (it == componentOffsets_.end())
	{
		return nullptr;
	}
	return chunks_[chunkIdx]->buffer + it->second;
}

Entity *Archetype::getEntityArray(uint32_t chunkIdx) const
{
	return reinterpret_cast<Entity *>(chunks_[chunkIdx]->buffer + entityArrayOffset_);
}

// -----------------------------------------------------------------------------
//  ECS – Main interface
// -----------------------------------------------------------------------------
class ECS
{
	public:
		ECS();
		~ECS() = default;

		Entity createEntity();
		void destroyEntity(Entity entity);
		bool alive(Entity entity) const;

		template <typename T>
		void addComponent(Entity entity, T value);

		template <typename T>
		void removeComponent(Entity entity);

		template <typename T>
		T *getComponent(Entity entity);

		template <typename... Components, typename Func>
		void forEach(Func &&func);

	private:
		std::vector<EntityRecord> records_;
		std::vector<uint32_t> freeIndices_;
		uint32_t nextEntityIndex_ = 0;
		std::unordered_map<ComponentMask, std::unique_ptr<Archetype>> archetypes_;

		Archetype *getOrCreateArchetype(ComponentMask mask);
		void moveEntity(Entity entity, ComponentMask newMask,
						const std::unordered_map<ComponentTypeId, const void *> &extraComponents = {});
		void *getComponentPtr(Entity entity, ComponentTypeId compId);
};

ECS::ECS() : records_(), freeIndices_(), nextEntityIndex_(0), archetypes_()
{
	getOrCreateArchetype(0);
}

bool ECS::alive(Entity entity) const
{
	if (entity.index >= records_.size())
	{
		return false;
	}
	const auto &rec = records_[entity.index];
	return rec.generation == entity.generation && rec.archetype != nullptr;
}

Entity ECS::createEntity()
{
	uint32_t idx;
	uint32_t gen = 1;
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
	}

	Entity e{idx, gen};
	records_[idx] = {gen, nullptr, 0, 0};

	Archetype *emptyArch = archetypes_[0].get();
	auto [chunk, slot] = emptyArch->addEntity(e, {});
	records_[idx].archetype = emptyArch;
	records_[idx].chunkIndex = chunk;
	records_[idx].slotIndex = slot;

	return e;
}

void ECS::destroyEntity(Entity entity)
{
	if (!alive(entity))
	{
		return;
	}

	auto &rec = records_[entity.index];
	Archetype *arch = rec.archetype;
	auto [movedEntity, newSlot] = arch->removeEntity(rec.chunkIndex, rec.slotIndex);

	if (movedEntity.index != 0)
	{
		auto &movedRec = records_[movedEntity.index];
		movedRec.archetype = arch;
		movedRec.chunkIndex = rec.chunkIndex;
		movedRec.slotIndex = newSlot;
	}

	rec.generation++;
	rec.archetype = nullptr;
	freeIndices_.push_back(entity.index);
}

Archetype *ECS::getOrCreateArchetype(ComponentMask mask)
{
	auto it = archetypes_.find(mask);
	if (it != archetypes_.end())
	{
		return it->second.get();
	}

	auto newArch = std::make_unique<Archetype>(mask);
	Archetype *ptr = newArch.get();
	archetypes_[mask] = std::move(newArch);
	return ptr;
}

void *ECS::getComponentPtr(Entity entity, ComponentTypeId compId)
{
	if (!alive(entity))
	{
		return nullptr;
	}
	auto &rec = records_[entity.index];
	Archetype *arch = rec.archetype;
	if (!(arch->getMask() & (ComponentMask(1) << compId)))
	{
		return nullptr;
	}
	void *arr = arch->getComponentArray(rec.chunkIndex, compId);
	if (!arr)
	{
		return nullptr;
	}
	size_t size = internal::getComponentRegistry().at(compId).size;
	return static_cast<std::byte *>(arr) + rec.slotIndex * size;
}

template <typename T>
void ECS::addComponent(Entity entity, T value)
{
	if (!alive(entity))
	{
		return;
	}

	ComponentTypeId compId = componentId<T>();
	ComponentMask oldMask = records_[entity.index].archetype->getMask();
	ComponentMask newMask = oldMask | (ComponentMask(1) << compId);

	if (oldMask == newMask)
	{
		T *ptr = static_cast<T *>(getComponentPtr(entity, compId));
		*ptr = std::move(value);
		return;
	}

	std::unordered_map<ComponentTypeId, const void *> extra;
	extra[compId] = &value;
	moveEntity(entity, newMask, extra);
}

template <typename T>
void ECS::removeComponent(Entity entity)
{
	if (!alive(entity))
	{
		return;
	}

	ComponentTypeId compId = componentId<T>();
	ComponentMask oldMask = records_[entity.index].archetype->getMask();
	if (!(oldMask & (ComponentMask(1) << compId)))
	{
		return;
	}

	ComponentMask newMask = oldMask & ~(ComponentMask(1) << compId);
	moveEntity(entity, newMask, {});
}

template <typename T>
T *ECS::getComponent(Entity entity)
{
	return static_cast<T *>(getComponentPtr(entity, componentId<T>()));
}

void ECS::moveEntity(Entity entity, ComponentMask newMask, const std::unordered_map<ComponentTypeId, const void *> &extraComponents)
{
	auto &rec = records_[entity.index];
	Archetype *srcArch = rec.archetype;
	ComponentMask oldMask = srcArch->getMask();
	if (oldMask == newMask)
	{
		return;
	}

	std::unordered_map<ComponentTypeId, const void *> allData = extraComponents;
	for (ComponentTypeId id = 0; id < MAX_COMPONENTS; ++id)
	{
		if (oldMask & (ComponentMask(1) << id))
		{
			if (allData.find(id) == allData.end())
			{
				void *ptr = getComponentPtr(entity, id);
				assert(ptr);
				allData[id] = ptr;
			}
		}
	}

	Archetype *dstArch = getOrCreateArchetype(newMask);
	auto [newChunk, newSlot] = dstArch->addEntity(entity, allData);
	auto [movedEntity, vacatedSlot] = srcArch->removeEntity(rec.chunkIndex, rec.slotIndex);

	rec.archetype = dstArch;
	rec.chunkIndex = newChunk;
	rec.slotIndex = newSlot;

	if (movedEntity.index != 0)
	{
		auto &movedRec = records_[movedEntity.index];
		movedRec.archetype = srcArch;
		movedRec.chunkIndex = rec.chunkIndex; // the old chunk index (still valid)
		movedRec.slotIndex = vacatedSlot;
	}
}

// -----------------------------------------------------------------------------
//  CORRECTED QUERY IMPLEMENTATION (no more std::get<void*>)
// -----------------------------------------------------------------------------
template <typename... Components, typename Func>
void ECS::forEach(Func &&func)
{
	ComponentMask requiredMask = ((ComponentMask(1) << componentId<Components>()) | ...);

	for (auto &[mask, archPtr] : archetypes_)
	{
		Archetype &arch = *archPtr;
		if ((mask & requiredMask) != requiredMask)
		{
			continue;
		}

		uint32_t chunkCount = arch.getChunkCount();
		for (uint32_t c = 0; c < chunkCount; ++c)
		{
			uint32_t entityCount = arch.getEntityCount(c);
			if (entityCount == 0)
			{
				continue;
			}

			Entity *entityArr = arch.getEntityArray(c);
			auto compArrays = std::make_tuple(arch.getComponentArray(c, componentId<Components>())...);

			auto invokeForSlot = [&](uint32_t s) {
				Entity e = entityArr[s];
				[&]<size_t... Is>(std::index_sequence<Is...>) {
					func(e, (*static_cast<Components *>(
								static_cast<void *>(static_cast<std::byte *>(std::get<Is>(compArrays)) + s * sizeof(Components))))...);
				}(std::index_sequence_for<Components...>{});
			};

			for (uint32_t s = 0; s < entityCount; ++s)
			{
				invokeForSlot(s);
			}
		}
	}
}