#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits> // if not already included
#include <unordered_map>
#include <vector>

// Detect whether T has a static constexpr bool is_tag, and if so, use its value.
template <typename T, typename = void>
struct is_tag_component : std::false_type
{};

template <typename T>
struct is_tag_component<T, std::void_t<decltype(T::is_tag)>> : std::integral_constant<bool, T::is_tag>
{};

// -----------------------------------------------------------------------------
//  Configuration
// -----------------------------------------------------------------------------
constexpr size_t CHUNK_SIZE = 16'384;
constexpr size_t MAX_COMPONENTS = 64;
using ComponentMask = uint64_t;
using ComponentTypeId = uint32_t;

// -----------------------------------------------------------------------------
//  Component Registry – type erased size, alignment, destructor, copy
// -----------------------------------------------------------------------------
struct ComponentInfo
{
		size_t size;
		size_t alignment;
		void (*destructor)(void *);
		void (*copyConstruct)(void *dest, const void *src);
		bool isTag;
};

namespace internal
{
	// **Declarations first** – fixes -Wmissing-declarations
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
												   [](void *dest, const void *src) { new (dest) T(*static_cast<const T *>(src)); }, false};
		return newId;
	}();
	return id;
}

template <typename T>
void registerTag()
{
	ComponentTypeId id = componentId<T>();
	internal::getComponentRegistry()[id].isTag = true;
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
		Archetype *archetype;
		uint32_t chunkIndex;
		uint32_t slotIndex;
};

// -----------------------------------------------------------------------------
//  Archetype – unique set of components (zero‑size tags stored as bits)
// -----------------------------------------------------------------------------
class Archetype
{
	public:
		explicit Archetype(ComponentMask regularMask);
		~Archetype();

		Archetype(const Archetype &) = delete;
		Archetype &operator=(const Archetype &) = delete;

		std::pair<uint32_t, uint32_t> addEntity(Entity entity, const std::array<const void *, MAX_COMPONENTS> &componentData,
												ComponentMask tags = 0);

		std::pair<Entity, uint32_t> removeEntity(uint32_t chunkIdx, uint32_t slotIdx);

		ComponentMask getRegularMask() const
		{
			return regularMask_;
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

		bool hasTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeId tagId) const;
		void setTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeId tagId);
		void clearTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeId tagId);
		ComponentMask getTags(uint32_t chunkIdx, uint32_t slotIdx) const;

		void compact(std::vector<EntityRecord> &globalRecords);

	private:
		struct Chunk
		{
				alignas(64) std::byte buffer[CHUNK_SIZE];
				uint32_t count = 0;
				uint32_t capacity = 0;
		};

		ComponentMask regularMask_;
		std::vector<std::unique_ptr<Chunk>> chunks_;
		std::vector<uint32_t> freeChunks_;
		uint32_t chunkCapacity_ = 0;

		std::array<size_t, MAX_COMPONENTS> componentOffsets_;
		std::array<size_t, MAX_COMPONENTS> componentSizes_;
		size_t entityArrayOffset_ = 0;
		size_t tagBitsetOffset_ = 0;
		std::vector<ComponentTypeId> sortedRegular_;

		uint32_t computeCapacity() const;
		void computeLayout(uint32_t capacity);

		uint64_t *getTagBitset(Chunk *chunk) const
		{
			return reinterpret_cast<uint64_t *>(chunk->buffer + tagBitsetOffset_);
		}

		const uint64_t *getTagBitset(const Chunk *chunk) const
		{
			return reinterpret_cast<const uint64_t *>(chunk->buffer + tagBitsetOffset_);
		}
};

// -----------------------------------------------------------------------------
//  Archetype implementation
// -----------------------------------------------------------------------------
Archetype::Archetype(ComponentMask regularMask)
	: regularMask_(regularMask), chunks_() // ✅ -Weffc++ fix
	  ,
	  freeChunks_(), chunkCapacity_(0), componentOffsets_() // value-initializes all to 0
	  ,
	  componentSizes_(), entityArrayOffset_(0), tagBitsetOffset_(0), sortedRegular_()
{
	// Fill offsets with SIZE_MAX sentinel
	componentOffsets_.fill(SIZE_MAX);
	componentSizes_.fill(0);

	for (ComponentTypeId id = 0; id < MAX_COMPONENTS; ++id)
	{
		if (regularMask_ & (ComponentMask(1) << id))
		{
			const auto &info = internal::getComponentRegistry().at(id);
			if (info.size > 0)
			{
				sortedRegular_.push_back(id);
			}
		}
	}
	std::sort(sortedRegular_.begin(), sortedRegular_.end());
	chunkCapacity_ = computeCapacity();
	computeLayout(chunkCapacity_);
}

Archetype::~Archetype()
{
	for (auto &chunk : chunks_)
	{
		for (uint32_t slot = 0; slot < chunk->count; ++slot)
		{
			for (ComponentTypeId compId : sortedRegular_)
			{
				void *ptr = static_cast<std::byte *>(chunk->buffer) + componentOffsets_[compId] + slot * componentSizes_[compId];
				internal::getComponentRegistry().at(compId).destructor(ptr);
			}
		}
	}
}

uint32_t Archetype::computeCapacity() const
{
	size_t perEntity = sizeof(Entity);
	for (ComponentTypeId id : sortedRegular_)
	{
		perEntity += internal::getComponentRegistry().at(id).size;
	}

	perEntity += sizeof(uint64_t); // tag bitset

	uint32_t cap = static_cast<uint32_t>(CHUNK_SIZE / perEntity) + 1;
	while (true)
	{
		size_t offset = 0;
		offset += cap * sizeof(Entity);
		offset = (offset + alignof(uint64_t) - 1) & ~(alignof(uint64_t) - 1);
		offset += cap * sizeof(uint64_t);

		for (ComponentTypeId id : sortedRegular_)
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

	offset = (offset + alignof(uint64_t) - 1) & ~(alignof(uint64_t) - 1);
	tagBitsetOffset_ = offset;
	offset += capacity * sizeof(uint64_t);

	for (ComponentTypeId id : sortedRegular_)
	{
		const auto &info = internal::getComponentRegistry().at(id);
		offset = (offset + info.alignment - 1) & ~(info.alignment - 1);
		componentOffsets_[id] = offset;
		componentSizes_[id] = info.size;
		offset += capacity * info.size;
	}
	assert(offset <= CHUNK_SIZE);
}

std::pair<uint32_t, uint32_t> Archetype::addEntity(Entity entity, const std::array<const void *, MAX_COMPONENTS> &componentData,
												   ComponentMask tags)
{
	Chunk *chunk = nullptr;
	uint32_t chunkIdx = 0;

	for (; chunkIdx < chunks_.size(); ++chunkIdx)
	{
		if (chunks_[chunkIdx]->count < chunkCapacity_)
		{
			chunk = chunks_[chunkIdx].get();
			break;
		}
	}

	if (!chunk && !freeChunks_.empty())
	{
		chunkIdx = freeChunks_.back();
		freeChunks_.pop_back();
		chunk = chunks_[chunkIdx].get();
		chunk->count = 0;
	}

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

	for (ComponentTypeId id : sortedRegular_)
	{
		const void *src = componentData[id];
		assert(src != nullptr);
		size_t offset = componentOffsets_[id];
		size_t size = componentSizes_[id];
		void *dest = chunk->buffer + offset + slot * size;
		internal::getComponentRegistry().at(id).copyConstruct(dest, src);
	}

	uint64_t *tagBits = getTagBitset(chunk);
	tagBits[slot] = tags;

	return {chunkIdx, slot};
}

std::pair<Entity, uint32_t> Archetype::removeEntity(uint32_t chunkIdx, uint32_t slotIdx)
{
	Chunk *chunk = chunks_[chunkIdx].get();
	assert(slotIdx < chunk->count);

	uint32_t lastSlot = chunk->count - 1;
	Entity *entityArr = reinterpret_cast<Entity *>(chunk->buffer + entityArrayOffset_);
	uint64_t *tagBits = getTagBitset(chunk);

	for (ComponentTypeId id : sortedRegular_)
	{
		size_t offset = componentOffsets_[id];
		size_t size = componentSizes_[id];
		void *ptr = chunk->buffer + offset + slotIdx * size;
		internal::getComponentRegistry().at(id).destructor(ptr);
	}

	Entity movedEntity{0, 0};
	if (slotIdx != lastSlot)
	{
		movedEntity = entityArr[lastSlot];
		entityArr[slotIdx] = movedEntity;
		tagBits[slotIdx] = tagBits[lastSlot];

		for (ComponentTypeId id : sortedRegular_)
		{
			size_t offset = componentOffsets_[id];
			size_t size = componentSizes_[id];
			void *dest = chunk->buffer + offset + slotIdx * size;
			void *src = chunk->buffer + offset + lastSlot * size;
			internal::getComponentRegistry().at(id).copyConstruct(dest, src);
		}
		for (ComponentTypeId id : sortedRegular_)
		{
			size_t offset = componentOffsets_[id];
			size_t size = componentSizes_[id];
			void *ptr = chunk->buffer + offset + lastSlot * size;
			internal::getComponentRegistry().at(id).destructor(ptr);
		}
	}

	--chunk->count;

	if (chunk->count == 0)
	{
		freeChunks_.push_back(chunkIdx);
	}

	return {movedEntity, slotIdx};
}

void *Archetype::getComponentArray(uint32_t chunkIdx, ComponentTypeId compId) const
{
	if (componentOffsets_[compId] == SIZE_MAX)
	{
		return nullptr;
	}
	return chunks_[chunkIdx]->buffer + componentOffsets_[compId];
}

Entity *Archetype::getEntityArray(uint32_t chunkIdx) const
{
	return reinterpret_cast<Entity *>(chunks_[chunkIdx]->buffer + entityArrayOffset_);
}

bool Archetype::hasTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeId tagId) const
{
	const uint64_t *tagBits = getTagBitset(chunks_[chunkIdx].get());
	return (tagBits[slotIdx] & (ComponentMask(1) << tagId)) != 0;
}

void Archetype::setTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeId tagId)
{
	uint64_t *tagBits = getTagBitset(chunks_[chunkIdx].get());
	tagBits[slotIdx] |= (ComponentMask(1) << tagId);
}

void Archetype::clearTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeId tagId)
{
	uint64_t *tagBits = getTagBitset(chunks_[chunkIdx].get());
	tagBits[slotIdx] &= ~(ComponentMask(1) << tagId);
}

ComponentMask Archetype::getTags(uint32_t chunkIdx, uint32_t slotIdx) const
{
	const uint64_t *tagBits = getTagBitset(chunks_[chunkIdx].get());
	return tagBits[slotIdx];
}

void Archetype::compact(std::vector<EntityRecord> &globalRecords)
{
	// Compute new size after compaction (remove empty chunks from end)
	size_t newSize = chunkCapacity_ == 0 ? 0 : (chunks_.size() - freeChunks_.size());
	if (freeChunks_.empty() && chunks_.size() == newSize)
	{
		return;
	}

	uint32_t writeIdx = 0;
	std::vector<uint32_t> newFree;
	for (uint32_t readIdx = 0; readIdx < chunks_.size(); ++readIdx)
	{
		if (chunks_[readIdx]->count > 0)
		{
			if (writeIdx != readIdx)
			{
				chunks_[writeIdx] = std::move(chunks_[readIdx]);
				Entity *entityArr = reinterpret_cast<Entity *>(chunks_[writeIdx]->buffer + entityArrayOffset_);
				for (uint32_t s = 0; s < chunks_[writeIdx]->count; ++s)
				{
					Entity e = entityArr[s];
					auto &rec = globalRecords[e.index];
					if (rec.archetype == this && rec.chunkIndex == readIdx)
					{
						rec.chunkIndex = writeIdx;
					}
				}
			}
			++writeIdx;
		}
		else
		{
			newFree.push_back(writeIdx);
		}
	}
	chunks_.resize(writeIdx);
	freeChunks_ = std::move(newFree);
}

// -----------------------------------------------------------------------------
//  Query cache – maps a component mask to a list of archetypes that contain it
// -----------------------------------------------------------------------------
class QueryCache
{
	public:
		QueryCache() : archetypes_() {} // explicitly defaulted – silences -Weffc++

		void addArchetype(ComponentMask regularMask, Archetype *arch)
		{
			archetypes_.emplace_back(regularMask, arch);
		}

		void removeArchetype(Archetype *arch)
		{
			auto it = std::remove_if(archetypes_.begin(), archetypes_.end(), [arch](const auto &p) { return p.second == arch; });
			archetypes_.erase(it, archetypes_.end());
		}

		// ✅ Return the vector directly – no mismatched types
		const std::vector<std::pair<ComponentMask, Archetype *>> &getAll() const
		{
			return archetypes_;
		}

	private:
		std::vector<std::pair<ComponentMask, Archetype *>> archetypes_;
};

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

		template <typename... Ts>
		Entity createEntityWith(Ts &&...components);

		template <typename T>
		void addComponent(Entity entity, T value);

		template <typename T>
		void removeComponent(Entity entity);

		template <typename T>
		T *getComponent(Entity entity);

		template <typename T>
		const T *getComponent(Entity entity) const;

		template <typename Tag>
		void addTag(Entity entity);

		template <typename Tag>
		void removeTag(Entity entity);

		template <typename Tag>
		bool hasTag(Entity entity) const;

		template <typename... Components, typename Func>
		void forEach(Func &&func);

		template <typename... Components, typename Func>
		void forEach(Func &&func) const;

		void compact()
		{
			for (auto &[mask, archPtr] : archetypes_)
			{
				archPtr->compact(records_);
			}
		}

	private:
		std::vector<EntityRecord> records_;
		std::vector<uint32_t> freeIndices_;
		uint32_t nextEntityIndex_ = 0;
		std::unordered_map<ComponentMask, std::unique_ptr<Archetype>> archetypes_;
		QueryCache queryCache_;

		Archetype *getOrCreateArchetype(ComponentMask regularMask);
		void moveEntity(Entity entity, ComponentMask newRegularMask, const std::array<const void *, MAX_COMPONENTS> &componentData,
						ComponentMask newTags = 0);
		void *getComponentPtr(Entity entity, ComponentTypeId compId);
		const void *getComponentPtr(Entity entity, ComponentTypeId compId) const;
		void updateQueryCache(Archetype *arch, ComponentMask mask, bool add);
};

// -----------------------------------------------------------------------------
//  ECS implementation
// -----------------------------------------------------------------------------
ECS::ECS() : records_(), freeIndices_(), nextEntityIndex_(0), archetypes_(), queryCache_() // ✅ -Weffc++ fix
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
	uint32_t gen;
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
	records_[idx] = {gen, nullptr, 0, 0};

	Archetype *emptyArch = archetypes_[0].get();
	std::array<const void *, MAX_COMPONENTS> noData{};
	noData.fill(nullptr);
	auto [chunk, slot] = emptyArch->addEntity(e, noData, 0);
	records_[idx].archetype = emptyArch;
	records_[idx].chunkIndex = chunk;
	records_[idx].slotIndex = slot;

	return e;
}

template <typename... Ts>
Entity ECS::createEntityWith(Ts &&...components)
{
	std::array<ComponentTypeId, sizeof...(Ts)> compIds{componentId<std::decay_t<Ts>>()...};
	std::array<const void *, sizeof...(Ts)> compPtrs{&components...};

	ComponentMask regularMask = 0;
	ComponentMask tagMask = 0;
	std::array<const void *, MAX_COMPONENTS> allData{};
	allData.fill(nullptr);

	for (size_t i = 0; i < sizeof...(Ts); ++i)
	{
		ComponentTypeId id = compIds[i];
		const auto &info = internal::getComponentRegistry().at(id);
		if (info.isTag)
		{
			tagMask |= (ComponentMask(1) << id);
		}
		else
		{
			regularMask |= (ComponentMask(1) << id);
			allData[id] = compPtrs[i];
		}
	}

	Entity e = createEntity();

	if (regularMask != 0 || tagMask != 0)
	{
		if (regularMask != 0)
		{
			moveEntity(e, regularMask, allData, tagMask);
		}
		else
		{
			moveEntity(e, 0, allData, tagMask);
		}
	}
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

Archetype *ECS::getOrCreateArchetype(ComponentMask regularMask)
{
	auto it = archetypes_.find(regularMask);
	if (it != archetypes_.end())
	{
		return it->second.get();
	}

	auto newArch = std::make_unique<Archetype>(regularMask);
	Archetype *ptr = newArch.get();
	archetypes_[regularMask] = std::move(newArch);
	updateQueryCache(ptr, regularMask, true);
	return ptr;
}

void ECS::updateQueryCache(Archetype *arch, ComponentMask mask, bool add)
{
	if (add)
	{
		queryCache_.addArchetype(mask, arch);
	}
	else
	{
		queryCache_.removeArchetype(arch);
	}
}

void *ECS::getComponentPtr(Entity entity, ComponentTypeId compId)
{
	if (!alive(entity))
	{
		return nullptr;
	}
	auto &rec = records_[entity.index];
	Archetype *arch = rec.archetype;
	if (!(arch->getRegularMask() & (ComponentMask(1) << compId)))
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

const void *ECS::getComponentPtr(Entity entity, ComponentTypeId compId) const
{
	if (!alive(entity))
	{
		return nullptr;
	}
	const auto &rec = records_[entity.index];
	const Archetype *arch = rec.archetype;
	if (!(arch->getRegularMask() & (ComponentMask(1) << compId)))
	{
		return nullptr;
	}
	const void *arr = arch->getComponentArray(rec.chunkIndex, compId);
	if (!arr)
	{
		return nullptr;
	}
	size_t size = internal::getComponentRegistry().at(compId).size;
	return static_cast<const std::byte *>(arr) + rec.slotIndex * size;
}

template <typename T>
void ECS::addComponent(Entity entity, T value)
{
	if (!alive(entity))
	{
		return;
	}
	ComponentTypeId compId = componentId<T>();
	const auto &info = internal::getComponentRegistry().at(compId);
	if (info.isTag)
	{
		auto &rec = records_[entity.index];
		rec.archetype->setTag(rec.chunkIndex, rec.slotIndex, compId);
		return;
	}

	ComponentMask oldRegular = records_[entity.index].archetype->getRegularMask();
	ComponentMask newRegular = oldRegular | (ComponentMask(1) << compId);
	if (oldRegular == newRegular)
	{
		T *ptr = static_cast<T *>(getComponentPtr(entity, compId));
		*ptr = std::move(value);
		return;
	}

	std::array<const void *, MAX_COMPONENTS> allData{};
	allData.fill(nullptr);
	allData[compId] = &value;
	moveEntity(entity, newRegular, allData);
}

template <typename T>
void ECS::removeComponent(Entity entity)
{
	if (!alive(entity))
	{
		return;
	}
	ComponentTypeId compId = componentId<T>();
	const auto &info = internal::getComponentRegistry().at(compId);
	if (info.isTag)
	{
		auto &rec = records_[entity.index];
		rec.archetype->clearTag(rec.chunkIndex, rec.slotIndex, compId);
		return;
	}

	ComponentMask oldRegular = records_[entity.index].archetype->getRegularMask();
	if (!(oldRegular & (ComponentMask(1) << compId)))
	{
		return;
	}
	ComponentMask newRegular = oldRegular & ~(ComponentMask(1) << compId);
	moveEntity(entity, newRegular, {});
}

template <typename T>
T *ECS::getComponent(Entity entity)
{
	return static_cast<T *>(getComponentPtr(entity, componentId<T>()));
}

template <typename T>
const T *ECS::getComponent(Entity entity) const
{
	return static_cast<const T *>(getComponentPtr(entity, componentId<T>()));
}

template <typename Tag>
void ECS::addTag(Entity entity)
{
	if (!alive(entity))
	{
		return;
	}
	ComponentTypeId tagId = componentId<Tag>();
	const auto &info = internal::getComponentRegistry().at(tagId);
	if (!info.isTag)
	{
		return; // not a tag – ignore
	}
	auto &rec = records_[entity.index];
	rec.archetype->setTag(rec.chunkIndex, rec.slotIndex, tagId);
}

template <typename Tag>
void ECS::removeTag(Entity entity)
{
	if (!alive(entity))
	{
		return;
	}
	ComponentTypeId tagId = componentId<Tag>();
	const auto &info = internal::getComponentRegistry().at(tagId);
	if (!info.isTag)
	{
		return;
	}
	auto &rec = records_[entity.index];
	rec.archetype->clearTag(rec.chunkIndex, rec.slotIndex, tagId);
}

template <typename Tag>
bool ECS::hasTag(Entity entity) const
{
	if (!alive(entity))
	{
		return false;
	}
	ComponentTypeId tagId = componentId<Tag>();
	const auto &info = internal::getComponentRegistry().at(tagId);
	if (!info.isTag)
	{
		return false;
	}
	const auto &rec = records_[entity.index];
	return rec.archetype->hasTag(rec.chunkIndex, rec.slotIndex, tagId);
}

void ECS::moveEntity(Entity entity, ComponentMask newRegularMask, const std::array<const void *, MAX_COMPONENTS> &componentData,
					 ComponentMask newTags)
{
	auto &rec = records_[entity.index];
	Archetype *srcArch = rec.archetype;
	ComponentMask oldRegular = srcArch->getRegularMask();
	ComponentMask oldTags = srcArch->getTags(rec.chunkIndex, rec.slotIndex);

	if (oldRegular == newRegularMask && oldTags == newTags)
	{
		return;
	}

	std::array<const void *, MAX_COMPONENTS> allData = componentData;
	for (ComponentTypeId id = 0; id < MAX_COMPONENTS; ++id)
	{
		if (oldRegular & (ComponentMask(1) << id))
		{
			if (allData[id] == nullptr)
			{
				allData[id] = getComponentPtr(entity, id);
			}
		}
	}

	Archetype *dstArch = getOrCreateArchetype(newRegularMask);
	auto [newChunk, newSlot] = dstArch->addEntity(entity, allData, newTags);
	auto [movedEntity, vacatedSlot] = srcArch->removeEntity(rec.chunkIndex, rec.slotIndex);

	rec.archetype = dstArch;
	rec.chunkIndex = newChunk;
	rec.slotIndex = newSlot;

	if (movedEntity.index != 0)
	{
		auto &movedRec = records_[movedEntity.index];
		movedRec.archetype = srcArch;
		movedRec.chunkIndex = rec.chunkIndex;
		movedRec.slotIndex = vacatedSlot;
	}
}

// -----------------------------------------------------------------------------
//  Query implementation – const and non‑const, with query cache
// -----------------------------------------------------------------------------
template <typename... Components, typename Func>
void ECS::forEach(Func &&func)
{
	// Compile-time tag detection – zero runtime overhead
	constexpr std::array<bool, sizeof...(Components)> isTag = {is_tag_component<Components>::value...};

	// Mask for regular components only (tags are not in archetype mask)
	ComponentMask requiredRegular = 0;
	(([&] {
		 if constexpr (!is_tag_component<Components>::value)
		 {
			 requiredRegular |= (ComponentMask(1) << componentId<Components>());
		 }
	 }()),
	 ...);

	for (auto &[mask, arch] : queryCache_.getAll())
	{
		if ((mask & requiredRegular) != requiredRegular)
		{
			continue;
		}

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

			for (uint32_t s = 0; s < entityCount; ++s)
			{
				Entity e = entityArr[s];

				// Verify entity has all required tags
				bool tagsOk = true;
				ComponentMask entityTags = arch->getTags(c, s);
				size_t i = 0;
				((tagsOk = tagsOk && (!isTag[i] || (entityTags & (ComponentMask(1) << componentId<Components>()))), ++i), ...);
				if (!tagsOk)
				{
					continue;
				}

				// Invoke user function – explicit return type stops inconsistent deduction
				[&]<size_t... Is>(std::index_sequence<Is...>) {
					func(e, ([&]() -> std::conditional_t<is_tag_component<Components>::value,
														 Components,	 // by value
														 Components &> { // by reference
							 if constexpr (is_tag_component<Components>::value)
							 {
								 return Components{};
							 }
							 else
							 {
								 return (static_cast<Components &>(*static_cast<Components *>(
									 static_cast<void *>(static_cast<std::byte *>(std::get<Is>(compArrays)) + s * sizeof(Components)))));
							 }
						 }())...);
				}(std::index_sequence_for<Components...>{});
			}
		}
	}
}

template <typename... Components, typename Func>
void ECS::forEach(Func &&func) const
{
	constexpr std::array<bool, sizeof...(Components)> isTag = {is_tag_component<Components>::value...};

	ComponentMask requiredRegular = 0;
	(([&] {
		 if constexpr (!is_tag_component<Components>::value)
		 {
			 requiredRegular |= (ComponentMask(1) << componentId<Components>());
		 }
	 }()),
	 ...);

	for (auto &[mask, arch] : queryCache_.getAll())
	{
		if ((mask & requiredRegular) != requiredRegular)
		{
			continue;
		}

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

			for (uint32_t s = 0; s < entityCount; ++s)
			{
				Entity e = entityArr[s];

				bool tagsOk = true;
				ComponentMask entityTags = arch->getTags(c, s);
				size_t i = 0;
				((tagsOk = tagsOk && (!isTag[i] || (entityTags & (ComponentMask(1) << componentId<Components>()))), ++i), ...);
				if (!tagsOk)
				{
					continue;
				}

				[&]<size_t... Is>(std::index_sequence<Is...>) {
					func(e, ([&]() -> std::conditional_t<is_tag_component<Components>::value, Components, const Components &> {
							 if constexpr (is_tag_component<Components>::value)
							 {
								 return Components{};
							 }
							 else
							 {
								 return (static_cast<const Components &>(*static_cast<const Components *>(static_cast<const void *>(
									 static_cast<const std::byte *>(std::get<Is>(compArrays)) + s * sizeof(Components)))));
							 }
						 }())...);
				}(std::index_sequence_for<Components...>{});
			}
		}
	}
}