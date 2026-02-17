#include <algorithm>
#include <atomic>
#include <bitset>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// -----------------------------------------------------------------------------
//  Forward declarations
// -----------------------------------------------------------------------------
struct Entity;
class ECS; // forward declaration for CommandBuffer

// -----------------------------------------------------------------------------
//  Component type definitions (user provided)
// -----------------------------------------------------------------------------
struct Position
{
		float x, y, z;
};

struct Velocity
{
		float dx, dy, dz;
};

struct Health
{
		int hp;
};

struct Mana
{
		int mp;
};

struct Buff
{
		std::string name;
		int duration;
};

struct Buffs
{
		std::vector<Buff> activeBuffs;
};

struct NameTag
{
		std::string tag;
};

// Zero‑size tags
struct AliveTag
{
		static constexpr bool is_tag = true;
};

struct DebugTag
{
		static constexpr bool is_tag = true;
};

struct BuffedTag
{
		static constexpr bool is_tag = true;
};

// -----------------------------------------------------------------------------
//  Compile‑time component registry
// -----------------------------------------------------------------------------
using ComponentTypes = std::tuple<Position, Velocity, Health, Mana, Buff, Buffs, NameTag, AliveTag, DebugTag, BuffedTag>;

// Helper to get index of a type in a tuple
template <typename T, typename Tuple>
struct tuple_index;

template <typename T, typename... Us>
struct tuple_index<T, std::tuple<T, Us...>> : std::integral_constant<std::size_t, 0>
{};

template <typename T, typename U, typename... Us>
struct tuple_index<T, std::tuple<U, Us...>> : std::integral_constant<std::size_t, 1 + tuple_index<T, std::tuple<Us...>>::value>
{};

template <typename T>
constexpr std::size_t componentId()
{
	static_assert(tuple_index<T, ComponentTypes>::value < std::tuple_size_v<ComponentTypes>,
				  "Component type not found in ComponentTypes list");
	return tuple_index<T, ComponentTypes>::value;
}

// Detect whether T has a static constexpr bool is_tag
template <typename T, typename = void>
struct is_tag_component : std::false_type
{};

template <typename T>
struct is_tag_component<T, std::void_t<decltype(T::is_tag)>> : std::integral_constant<bool, T::is_tag>
{};

template <typename>
struct always_false : std::false_type
{};

// -----------------------------------------------------------------------------
//  Configuration
// -----------------------------------------------------------------------------
constexpr std::size_t CHUNK_SIZE = 16'384;
constexpr std::size_t MAX_COMPONENTS = 128;
using ComponentTypeID = uint32_t;
using VersionType = uint64_t;

// Forward declarations
class Archetype;

// Chunk version for change detection
struct ChunkVersion
{
		VersionType version;
		std::array<VersionType, MAX_COMPONENTS> componentVersions;

		ChunkVersion() : version(1)
		{
			componentVersions.fill(1);
		}

		void bump()
		{
			++version;
		}

		void bumpComponent(ComponentTypeID id)
		{
			++componentVersions[id];
		}
};

// 128‑bit component mask
struct ComponentMask
{
		uint64_t low;
		uint64_t high;

		constexpr ComponentMask() : low(0), high(0) {}

		constexpr ComponentMask(uint64_t l) : low(l), high(0) {}

		constexpr ComponentMask(uint64_t l, uint64_t h) : low(l), high(h) {}

		constexpr ComponentMask operator&(const ComponentMask &other) const
		{
			return {low & other.low, high & other.high};
		}

		constexpr ComponentMask operator|(const ComponentMask &other) const
		{
			return {low | other.low, high | other.high};
		}

		constexpr ComponentMask operator^(const ComponentMask &other) const
		{
			return {low ^ other.low, high ^ other.high};
		}

		constexpr ComponentMask operator~() const
		{
			return {~low, ~high};
		}

		constexpr ComponentMask operator<<(int shift) const
		{
			if (shift < 64)
			{
				return {low << shift, (high << shift) | (low >> (64 - shift))};
			}
			else if (shift < 128)
			{
				return {0, low << (shift - 64)};
			}
			else
			{
				return {0, 0};
			}
		}

		constexpr ComponentMask operator>>(int shift) const
		{
			if (shift < 64)
			{
				return {(low >> shift) | (high << (64 - shift)), high >> shift};
			}
			else if (shift < 128)
			{
				return {high >> (shift - 64), 0};
			}
			else
			{
				return {0, 0};
			}
		}

		constexpr ComponentMask &operator&=(const ComponentMask &other)
		{
			low &= other.low;
			high &= other.high;
			return *this;
		}

		constexpr ComponentMask &operator|=(const ComponentMask &other)
		{
			low |= other.low;
			high |= other.high;
			return *this;
		}

		constexpr ComponentMask &operator^=(const ComponentMask &other)
		{
			low ^= other.low;
			high ^= other.high;
			return *this;
		}

		constexpr bool operator==(const ComponentMask &other) const
		{
			return low == other.low && high == other.high;
		}

		constexpr bool operator!=(const ComponentMask &other) const
		{
			return low != other.low || high != other.high;
		}

		constexpr explicit operator bool() const
		{
			return low != 0 || high != 0;
		}
};

// Hash support for unordered_map
namespace std
{
	template <>
	struct hash<ComponentMask>
	{
			std::size_t operator()(const ComponentMask &m) const noexcept
			{
				return hash<uint64_t>{}(m.low) ^ (hash<uint64_t>{}(m.high) << 1);
			}
	};
} // namespace std

// Helper to iterate over set bits in a ComponentMask
template <typename F>
void forEachSetBit(ComponentMask mask, F &&func)
{
	uint64_t bits = mask.low;
	while (bits)
	{
		uint64_t t = bits & -bits;
		int idx = __builtin_ctzll(bits);
		func(static_cast<ComponentTypeID>(idx));
		bits ^= t;
	}
	bits = mask.high;
	while (bits)
	{
		uint64_t t = bits & -bits;
		int idx = __builtin_ctzll(bits) + 64;
		func(static_cast<ComponentTypeID>(idx));
		bits ^= t;
	}
}

// Simple latch for parallel task counting
class Latch
{
	public:
		explicit Latch(int count) : counter(count) {}

		void count_down()
		{
			if (counter.fetch_sub(1, std::memory_order_acq_rel) == 1)
			{
				cv.notify_all();
			}
		}

		void wait()
		{
			std::unique_lock<std::mutex> lock(mutex);
			cv.wait(lock, [this] { return counter.load(std::memory_order_acquire) == 0; });
		}

	private:
		std::atomic<int> counter;
		std::mutex mutex;
		std::condition_variable cv;
};

// Work-stealing task queue
class WorkStealingQueue
{
	public:
		using Task = std::function<void()>;

		void push(Task task)
		{
			std::unique_lock<std::mutex> lock(mutex);
			queue.push_back(std::move(task));
		}

		bool try_pop(Task &task)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (queue.empty())
			{
				return false;
			}
			task = std::move(queue.front());
			queue.pop_front();
			return true;
		}

		bool try_steal(Task &task)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (queue.empty())
			{
				return false;
			}
			task = std::move(queue.back());
			queue.pop_back();
			return true;
		}

		std::size_t size() const
		{
			std::unique_lock<std::mutex> lock(mutex);
			return queue.size();
		}

	private:
		std::deque<Task> queue;
		mutable std::mutex mutex;
};

// Work-stealing thread pool with batch scheduling
class WorkStealingPool
{
	public:
		WorkStealingPool(std::size_t numThreads = std::thread::hardware_concurrency()) : stop(false), taskCount(0), queues(numThreads)
		{
			for (std::size_t i = 0; i < numThreads; ++i)
			{
				workers.emplace_back([this, i] { worker_loop(i); });
			}
		}

		~WorkStealingPool()
		{
			stop = true;
			for (auto &q : queues)
			{
				q.push(nullptr); // Sentinel to wake up
			}
			for (auto &worker : workers)
			{
				worker.join();
			}
		}

		// Submit a batch of chunks
		template <typename TaskFunc>
		void submit_chunks(const std::vector<std::pair<Archetype *, uint32_t>> &chunks, TaskFunc &&func, Latch &latch,
						   std::size_t batchSize = 4)
		{
			std::vector<std::pair<Archetype *, uint32_t>> batch;
			batch.reserve(batchSize);

			for (const auto &chunk : chunks)
			{
				batch.push_back(chunk);
				if (batch.size() >= batchSize)
				{
					auto task = [batch, func, &latch]() {
						for (const auto &[arch, chunkIdx] : batch)
						{
							func(arch, chunkIdx);
						}
						latch.count_down();
					};
					submit_task(std::move(task));
					batch.clear();
				}
			}

			if (!batch.empty())
			{
				auto task = [batch, func, &latch]() {
					for (const auto &[arch, chunkIdx] : batch)
					{
						func(arch, chunkIdx);
					}
					latch.count_down();
				};
				submit_task(std::move(task));
			}
		}

	private:
		void submit_task(std::function<void()> task)
		{
			std::size_t idx = taskCount++ % queues.size();
			queues[idx].push(std::move(task));
		}

		void worker_loop(std::size_t workerId)
		{
			while (!stop)
			{
				std::function<void()> task;

				if (queues[workerId].try_pop(task))
				{
					if (!task)
					{
						break;
					}
					task();
					continue;
				}

				for (std::size_t i = 1; i < queues.size(); ++i)
				{
					std::size_t victimId = (workerId + i) % queues.size();
					if (queues[victimId].try_steal(task))
					{
						if (!task)
						{
							break;
						}
						task();
						break;
					}
				}

				if (!task)
				{
					std::this_thread::yield();
				}
			}
		}

		std::vector<std::thread> workers;
		std::vector<WorkStealingQueue> queues;
		std::atomic<bool> stop;
		std::atomic<std::size_t> taskCount;
};

// Execution policy for forEach
enum class ExecutionPolicy
{
	Seq,		// sequential (default)
	Par,		// parallel – one task per chunk
	ParBatched, // parallel – batched chunks per task
	ParStealing // parallel – work stealing with batching
};

// -----------------------------------------------------------------------------
//  ComponentInfo – type‑erased operations
// -----------------------------------------------------------------------------
struct ComponentInfo
{
		std::size_t size;
		std::size_t alignment;
		void (*destructor)(void *);
		void (*copyConstruct)(void *dest, const void *src);
		void (*moveConstruct)(void *dest, void *src);
		bool isTag;
};

// Build a constexpr array of ComponentInfo from the type list
template <typename... Ts>
constexpr std::array<ComponentInfo, sizeof...(Ts)> make_component_infos(std::tuple<Ts...>)
{
	return {{{sizeof(Ts), alignof(Ts), [](void *ptr) { static_cast<Ts *>(ptr)->~Ts(); },
			  [](void *dest, const void *src) { new (dest) Ts(*static_cast<const Ts *>(src)); },
			  [](void *dest, void *src) {
				  if constexpr (std::is_move_constructible_v<Ts>)
				  {
					  new (dest) Ts(std::move(*static_cast<Ts *>(src)));
				  }
				  else if constexpr (std::is_copy_constructible_v<Ts>)
				  {
					  new (dest) Ts(*static_cast<const Ts *>(src));
				  }
				  else
				  {
					  static_assert(always_false<Ts>::value, "Component must be copy or move constructible");
				  }
			  },
			  is_tag_component<Ts>::value}...}};
}

// Global compile‑time info array (indexed by componentId)
constexpr auto ComponentInfos = make_component_infos(ComponentTypes{});

// Compute maximum component size and alignment at compile time
template <typename>
struct MaxSizeHelper;

template <typename... Ts>
struct MaxSizeHelper<std::tuple<Ts...>>
{
		static constexpr std::size_t size = std::max({sizeof(Ts)...});
		static constexpr std::size_t alignment = std::max({alignof(Ts)...});
};

constexpr std::size_t MAX_COMPONENT_SIZE = MaxSizeHelper<ComponentTypes>::size;
constexpr std::size_t MAX_COMPONENT_ALIGN = MaxSizeHelper<ComponentTypes>::alignment;

// -----------------------------------------------------------------------------
//  Entity – generational handle
// -----------------------------------------------------------------------------
struct Entity
{
		uint32_t index;
		uint32_t generation;
		bool operator==(const Entity &other) const = default;
		bool operator!=(const Entity &other) const = default;
};

// Null entity constant
constexpr Entity NULL_ENTITY = {0, 0};

// -----------------------------------------------------------------------------
//  Command Buffer – per‑thread, lock‑free command generation with tagged union
// -----------------------------------------------------------------------------
class CommandBuffer
{
	private:
		enum class CmdType : uint8_t
		{
			AddComponent,
			RemoveComponent,
			Destroy,
			SetParent
		};

		struct Command
		{
				CmdType type;
				Entity entity;

				union {
						struct
						{
								ComponentTypeID compId;
								alignas(MAX_COMPONENT_ALIGN) std::byte buffer[MAX_COMPONENT_SIZE];
						} add;

						struct
						{
								ComponentTypeID compId;
						} remove;

						struct
						{
								Entity parent;
						} setParent;
				} data;

				template <typename T>
				static Command makeAdd(Entity e, T &&value)
				{
					Command cmd;
					cmd.type = CmdType::AddComponent;
					cmd.entity = e;
					cmd.data.add.compId = componentId<T>();
					new (cmd.data.add.buffer) T(std::forward<T>(value));
					return cmd;
				}

				static Command makeRemove(Entity e, ComponentTypeID compId)
				{
					Command cmd;
					cmd.type = CmdType::RemoveComponent;
					cmd.entity = e;
					cmd.data.remove.compId = compId;
					return cmd;
				}

				static Command makeDestroy(Entity e)
				{
					Command cmd;
					cmd.type = CmdType::Destroy;
					cmd.entity = e;
					return cmd;
				}

				static Command makeSetParent(Entity child, Entity parent)
				{
					Command cmd;
					cmd.type = CmdType::SetParent;
					cmd.entity = child;
					cmd.data.setParent.parent = parent;
					return cmd;
				}

				void destroyBuffer()
				{
					if (type == CmdType::AddComponent)
					{
						ComponentInfos[data.add.compId].destructor(data.add.buffer);
					}
				}
		};

		struct ThreadBuffer
		{
				std::vector<Command> commands;
		};

		std::unordered_map<std::thread::id, std::unique_ptr<ThreadBuffer>> buffers_;
		std::mutex map_mutex_;

		ThreadBuffer *getThreadBuffer()
		{
			std::lock_guard<std::mutex> lock(map_mutex_);
			auto tid = std::this_thread::get_id();
			auto it = buffers_.find(tid);
			if (it != buffers_.end())
			{
				return it->second.get();
			}
			auto buf = std::make_unique<ThreadBuffer>();
			ThreadBuffer *ptr = buf.get();
			buffers_[tid] = std::move(buf);
			return ptr;
		}

		// Helper templates – declared, defined later after ECS is complete
		template <typename... Ts>
		static void dispatchAddImpl(ECS &ecs, Entity e, ComponentTypeID id, void *buffer, std::tuple<Ts...>);

		static void dispatchAdd(ECS &ecs, Entity e, ComponentTypeID id, void *buffer);

		static void dispatchRemove(ECS &ecs, Entity e, ComponentTypeID id);

	public:
		template <typename T>
		void addComponent(Entity entity, T value)
		{
			auto buf = getThreadBuffer();
			buf->commands.push_back(Command::makeAdd(entity, std::move(value)));
		}

		template <typename T>
		void removeComponent(Entity entity)
		{
			auto buf = getThreadBuffer();
			ComponentTypeID compId = componentId<T>();
			buf->commands.push_back(Command::makeRemove(entity, compId));
		}

		void destroy(Entity entity)
		{
			auto buf = getThreadBuffer();
			buf->commands.push_back(Command::makeDestroy(entity));
		}

		void setParent(Entity child, Entity parent)
		{
			auto buf = getThreadBuffer();
			buf->commands.push_back(Command::makeSetParent(child, parent));
		}

		void apply(ECS &ecs);

		void clear()
		{
			std::lock_guard<std::mutex> lock(map_mutex_);
			buffers_.clear();
		}
};

// -----------------------------------------------------------------------------
//  Thread pool (kept for backward compatibility)
// -----------------------------------------------------------------------------
class ThreadPool
{
	public:
		ThreadPool(std::size_t numThreads = std::thread::hardware_concurrency()) : stop(false)
		{
			for (std::size_t i = 0; i < numThreads; ++i)
			{
				workers.emplace_back([this] {
					while (true)
					{
						std::function<void()> task;
						{
							std::unique_lock<std::mutex> lock(queueMutex);
							condition.wait(lock, [this] { return stop || !tasks.empty(); });
							if (stop && tasks.empty())
							{
								return;
							}
							task = std::move(tasks.front());
							tasks.pop();
						}
						task();
					}
				});
			}
		}

		~ThreadPool()
		{
			{
				std::unique_lock<std::mutex> lock(queueMutex);
				stop = true;
			}
			condition.notify_all();
			for (std::thread &worker : workers)
			{
				worker.join();
			}
		}

		template <typename F>
		auto submit(F &&f) const -> std::future<decltype(f())>
		{
			using return_type = decltype(f());
			auto task = std::make_shared<std::packaged_task<return_type()>>(std::forward<F>(f));
			std::future<return_type> result = task->get_future();
			{
				std::unique_lock<std::mutex> lock(queueMutex);
				tasks.emplace([task]() { (*task)(); });
			}
			condition.notify_one();
			return result;
		}

		void submit_with_latch(std::function<void()> task, Latch &latch) const
		{
			{
				std::unique_lock<std::mutex> lock(queueMutex);
				tasks.emplace([task = std::move(task), &latch]() {
					task();
					latch.count_down();
				});
			}
			condition.notify_one();
		}

	private:
		mutable std::queue<std::function<void()>> tasks;
		mutable std::mutex queueMutex;
		mutable std::condition_variable condition;
		std::vector<std::thread> workers;
		std::atomic<bool> stop;
};

// -----------------------------------------------------------------------------
//  Forward declarations for EntityRecord
// -----------------------------------------------------------------------------
struct EntityRecord
{
		uint32_t generation;
		Archetype *archetype;
		uint32_t chunkIndex;
		uint32_t slotIndex;
};

// Per-system last seen version
struct SystemVersion
{
		VersionType version;
		std::array<VersionType, MAX_COMPONENTS> componentVersions;

		SystemVersion() : version(0)
		{
			componentVersions.fill(0);
		}

		bool needsUpdate(const ChunkVersion &chunk, ComponentMask requiredComponents) const
		{
			if (chunk.version > version)
			{
				return true;
			}

			bool needs = false;
			forEachSetBit(requiredComponents, [&](ComponentTypeID id) {
				if (chunk.componentVersions[id] > componentVersions[id])
				{
					needs = true;
				}
			});
			return needs;
		}

		void update(const ChunkVersion &chunk)
		{
			version = chunk.version;
			// componentVersions intentionally not updated here – they are updated per‑chunk after processing
		}
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

		std::pair<uint32_t, uint32_t> addEntity(Entity entity, const std::array<const void *, MAX_COMPONENTS> &copyData,
												const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask tags = ComponentMask(0));

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

		void *getComponentArray(uint32_t chunkIdx, ComponentTypeID compId) const;
		Entity *getEntityArray(uint32_t chunkIdx) const;

		bool hasTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeID tagId) const;
		void setTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeID tagId);
		void clearTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeID tagId);
		ComponentMask getTags(uint32_t chunkIdx, uint32_t slotIdx) const;

		void compact(std::vector<EntityRecord> &globalRecords);

		// Versioning
		const ChunkVersion &getChunkVersion(uint32_t chunkIdx) const
		{
			return chunkVersions_[chunkIdx];
		}

		void bumpChunkVersion(uint32_t chunkIdx)
		{
			chunkVersions_[chunkIdx].bump();
		}

		void bumpComponentVersion(uint32_t chunkIdx, ComponentTypeID compId)
		{
			chunkVersions_[chunkIdx].bumpComponent(compId);
		}

		// Helper to iterate over set bits in the regular mask
		template <typename F>
		void forEachComponent(F &&func) const
		{
			forEachSetBit(regularMask_, std::forward<F>(func));
		}

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

		std::array<std::size_t, MAX_COMPONENTS> componentOffsets_;
		std::array<std::size_t, MAX_COMPONENTS> componentSizes_;
		std::size_t entityArrayOffset_ = 0;
		std::size_t tagBitsetOffset_ = 0;
		std::vector<ComponentTypeID> sortedRegular_;

		// Per-chunk versions
		std::vector<ChunkVersion> chunkVersions_;

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
Archetype::Archetype(ComponentMask regularMask) : regularMask_(regularMask), chunks_(), freeChunks_(), chunkCapacity_(0), chunkVersions_()
{
	componentOffsets_.fill(SIZE_MAX);
	componentSizes_.fill(0);

	forEachSetBit(regularMask_, [this](ComponentTypeID id) {
		const auto &info = ComponentInfos[id];
		if (info.size > 0)
		{
			sortedRegular_.push_back(id);
		}
	});

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
			for (ComponentTypeID compId : sortedRegular_)
			{
				void *ptr = static_cast<std::byte *>(chunk->buffer) + componentOffsets_[compId] + slot * componentSizes_[compId];
				ComponentInfos[compId].destructor(ptr);
			}
		}
	}
}

uint32_t Archetype::computeCapacity() const
{
	std::size_t perEntity = sizeof(Entity);
	for (ComponentTypeID id : sortedRegular_)
	{
		perEntity += ComponentInfos[id].size;
	}

	perEntity += sizeof(uint64_t); // tag bitset

	uint32_t cap = static_cast<uint32_t>(CHUNK_SIZE / perEntity) + 1;
	while (true)
	{
		std::size_t offset = 0;
		offset += cap * sizeof(Entity);
		offset = (offset + alignof(uint64_t) - 1) & ~(alignof(uint64_t) - 1);
		offset += cap * sizeof(uint64_t);

		for (ComponentTypeID id : sortedRegular_)
		{
			const auto &info = ComponentInfos[id];
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
	std::size_t offset = 0;
	entityArrayOffset_ = offset;
	offset += capacity * sizeof(Entity);

	offset = (offset + alignof(uint64_t) - 1) & ~(alignof(uint64_t) - 1);
	tagBitsetOffset_ = offset;
	offset += capacity * sizeof(uint64_t);

	for (ComponentTypeID id : sortedRegular_)
	{
		const auto &info = ComponentInfos[id];
		offset = (offset + info.alignment - 1) & ~(info.alignment - 1);
		componentOffsets_[id] = offset;
		componentSizes_[id] = info.size;
		offset += capacity * info.size;
	}
	assert(offset <= CHUNK_SIZE);
}

std::pair<uint32_t, uint32_t> Archetype::addEntity(Entity entity, const std::array<const void *, MAX_COMPONENTS> &copyData,
												   const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask tags)
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
		if (chunkIdx >= chunks_.size())
		{
			// Defensive: invalid free index – should not happen, but fallback
			chunkIdx = chunks_.size();
			auto newChunk = std::make_unique<Chunk>();
			newChunk->capacity = chunkCapacity_;
			chunk = newChunk.get();
			chunks_.push_back(std::move(newChunk));
			chunkVersions_.emplace_back();
		}
		else
		{
			chunk = chunks_[chunkIdx].get();
			chunk->count = 0;
		}
	}

	if (!chunk)
	{
		auto newChunk = std::make_unique<Chunk>();
		newChunk->capacity = chunkCapacity_;
		chunk = newChunk.get();
		chunks_.push_back(std::move(newChunk));
		chunkVersions_.emplace_back(); // Add version for new chunk
		chunkIdx = static_cast<uint32_t>(chunks_.size() - 1);
	}

	uint32_t slot = chunk->count++;
	Entity *entityArr = reinterpret_cast<Entity *>(chunk->buffer + entityArrayOffset_);
	new (&entityArr[slot]) Entity(entity);

	for (ComponentTypeID id : sortedRegular_)
	{
		std::size_t offset = componentOffsets_[id];
		std::size_t size = componentSizes_[id];
		void *dest = chunk->buffer + offset + slot * size;

		const auto &info = ComponentInfos[id];
		if (moveData[id] != nullptr)
		{
			info.moveConstruct(dest, moveData[id]);
		}
		else
		{
			assert(copyData[id] != nullptr);
			info.copyConstruct(dest, copyData[id]);
		}
	}

	uint64_t *tagBits = getTagBitset(chunk);
	tagBits[slot] = tags.low;

	// Bump chunk version
	chunkVersions_[chunkIdx].bump();

	return {chunkIdx, slot};
}

std::pair<Entity, uint32_t> Archetype::removeEntity(uint32_t chunkIdx, uint32_t slotIdx)
{
	Chunk *chunk = chunks_[chunkIdx].get();
	assert(slotIdx < chunk->count);

	uint32_t lastSlot = chunk->count - 1;
	Entity *entityArr = reinterpret_cast<Entity *>(chunk->buffer + entityArrayOffset_);
	uint64_t *tagBits = getTagBitset(chunk);

	for (ComponentTypeID id : sortedRegular_)
	{
		std::size_t offset = componentOffsets_[id];
		std::size_t size = componentSizes_[id];
		void *ptr = chunk->buffer + offset + slotIdx * size;
		ComponentInfos[id].destructor(ptr);
	}

	Entity movedEntity{0, 0};
	if (slotIdx != lastSlot)
	{
		movedEntity = entityArr[lastSlot];
		entityArr[slotIdx] = movedEntity;
		tagBits[slotIdx] = tagBits[lastSlot];

		for (ComponentTypeID id : sortedRegular_)
		{
			std::size_t offset = componentOffsets_[id];
			std::size_t size = componentSizes_[id];
			void *dest = chunk->buffer + offset + slotIdx * size;
			void *src = chunk->buffer + offset + lastSlot * size;
			ComponentInfos[id].moveConstruct(dest, src);
		}
		for (ComponentTypeID id : sortedRegular_)
		{
			std::size_t offset = componentOffsets_[id];
			std::size_t size = componentSizes_[id];
			void *ptr = chunk->buffer + offset + lastSlot * size;
			ComponentInfos[id].destructor(ptr);
		}
	}

	--chunk->count;

	// Bump chunk version
	chunkVersions_[chunkIdx].bump();

	if (chunk->count == 0)
	{
		freeChunks_.push_back(chunkIdx);
	}

	return {movedEntity, slotIdx};
}

void *Archetype::getComponentArray(uint32_t chunkIdx, ComponentTypeID compId) const
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

bool Archetype::hasTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeID tagId) const
{
	const uint64_t *tagBits = getTagBitset(chunks_[chunkIdx].get());
	return (tagBits[slotIdx] & (uint64_t(1) << tagId)) != 0;
}

void Archetype::setTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeID tagId)
{
	uint64_t *tagBits = getTagBitset(chunks_[chunkIdx].get());
	tagBits[slotIdx] |= (uint64_t(1) << tagId);
	chunkVersions_[chunkIdx].bump(); // Bump version
}

void Archetype::clearTag(uint32_t chunkIdx, uint32_t slotIdx, ComponentTypeID tagId)
{
	uint64_t *tagBits = getTagBitset(chunks_[chunkIdx].get());
	tagBits[slotIdx] &= ~(uint64_t(1) << tagId);
	chunkVersions_[chunkIdx].bump(); // Bump version
}

ComponentMask Archetype::getTags(uint32_t chunkIdx, uint32_t slotIdx) const
{
	const uint64_t *tagBits = getTagBitset(chunks_[chunkIdx].get());
	return ComponentMask(tagBits[slotIdx], 0);
}

void Archetype::compact(std::vector<EntityRecord> &globalRecords)
{
	std::size_t newSize = chunkCapacity_ == 0 ? 0 : (chunks_.size() - freeChunks_.size());
	if (freeChunks_.empty() && chunks_.size() == newSize)
	{
		return;
	}

	uint32_t writeIdx = 0;
	for (uint32_t readIdx = 0; readIdx < chunks_.size(); ++readIdx)
	{
		if (chunks_[readIdx]->count > 0)
		{
			if (writeIdx != readIdx)
			{
				chunks_[writeIdx] = std::move(chunks_[readIdx]);
				chunkVersions_[writeIdx] = chunkVersions_[readIdx]; // Move version
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
		// else: empty chunk – just skip
	}
	chunks_.resize(writeIdx);
	chunkVersions_.resize(writeIdx);
	freeChunks_.clear(); // After compaction, all remaining chunks are non‑empty
}

// -----------------------------------------------------------------------------
//  Query cache
// -----------------------------------------------------------------------------
class QueryCache
{
	public:
		void addArchetype(ComponentMask regularMask, Archetype *arch)
		{
			archetypes_.emplace_back(regularMask, arch);
			results_.clear();
		}

		void removeArchetype(Archetype *arch)
		{
			auto it = std::remove_if(archetypes_.begin(), archetypes_.end(), [arch](const auto &p) { return p.second == arch; });
			archetypes_.erase(it, archetypes_.end());
			results_.clear();
		}

		const std::vector<Archetype *> &get(ComponentMask requiredMask) const
		{
			auto it = results_.find(requiredMask);
			if (it != results_.end())
			{
				return it->second;
			}

			std::vector<Archetype *> matching;
			for (auto &[mask, arch] : archetypes_)
			{
				if ((mask & requiredMask) == requiredMask)
				{
					matching.push_back(arch);
				}
			}
			auto emplaceResult = results_.emplace(requiredMask, std::move(matching));
			return emplaceResult.first->second;
		}

		void clear()
		{
			results_.clear();
		}

	private:
		std::vector<std::pair<ComponentMask, Archetype *>> archetypes_;
		mutable std::unordered_map<ComponentMask, std::vector<Archetype *>> results_;
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

		// Non‑template removeComponent (for command buffer)
		void removeComponent(Entity entity, ComponentTypeID compId);

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

		// Hierarchy functions (no component)
		void setParent(Entity child, Entity parent);
		std::vector<Entity> getChildren(Entity parent) const;
		Entity getParent(Entity child) const;

		// Sequential forEach (default)
		template <typename... Components, typename Func>
		void forEach(Func &&func);

		template <typename... Components, typename Func>
		void forEach(Func &&func) const;

		// Parallel forEach with explicit policy
		template <typename... Components, typename Func>
		void forEach(ExecutionPolicy policy, Func &&func);

		template <typename... Components, typename Func>
		void forEach(ExecutionPolicy policy, Func &&func) const;

		// Version-aware forEach with system version tracking
		template <typename... Components, typename Func>
		void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func);

		template <typename... Components, typename Func>
		void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const;

		// forEach with command buffer (deferred modifications)
		template <typename... Components, typename Func>
		void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func);

		template <typename... Components, typename Func>
		void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const = delete; // not allowed

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
		mutable ThreadPool threadPool_;				// For backward compatibility
		mutable WorkStealingPool workStealingPool_; // Work-stealing pool

		// Hierarchy storage – dense vectors indexed by entity index
		mutable std::mutex hierarchyMutex_;
		std::vector<Entity> parent_;				// parent_[idx] = parent entity (or NULL_ENTITY)
		std::vector<std::vector<Entity>> children_; // children_[idx] = list of child entities

		Archetype *getOrCreateArchetype(ComponentMask regularMask);
		void moveEntity(Entity entity, ComponentMask newRegularMask, const std::array<const void *, MAX_COMPONENTS> &copyData,
						const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask newTags = ComponentMask(0));
		void *getComponentPtr(Entity entity, ComponentTypeID compId);
		const void *getComponentPtr(Entity entity, ComponentTypeID compId) const;

		// Helper to recursively destroy children
		void destroyHierarchy(Entity entity);

		// Friend for command buffer commands
		friend class CommandBuffer;
};

// -----------------------------------------------------------------------------
//  ECS implementation
// -----------------------------------------------------------------------------
ECS::ECS()
	: records_(), freeIndices_(), nextEntityIndex_(0), archetypes_(), queryCache_(), threadPool_(), workStealingPool_(), hierarchyMutex_(),
	  parent_(), children_()
{
	getOrCreateArchetype(ComponentMask(0));
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

	Archetype *emptyArch = archetypes_[ComponentMask(0)].get();
	std::array<const void *, MAX_COMPONENTS> noCopy{};
	std::array<void *, MAX_COMPONENTS> noMove{};
	noCopy.fill(nullptr);
	noMove.fill(nullptr);
	auto [chunk, slot] = emptyArch->addEntity(e, noCopy, noMove, ComponentMask(0));
	records_[idx].archetype = emptyArch;
	records_[idx].chunkIndex = chunk;
	records_[idx].slotIndex = slot;

	// Ensure hierarchy vectors are large enough
	{
		std::lock_guard<std::mutex> lock(hierarchyMutex_);
		if (parent_.size() <= idx)
		{
			parent_.resize(idx + 1, NULL_ENTITY);
			children_.resize(idx + 1);
		}
	}

	return e;
}

template <typename... Ts>
Entity ECS::createEntityWith(Ts &&...components)
{
	std::array<ComponentTypeID, sizeof...(Ts)> compIds{componentId<std::decay_t<Ts>>()...};

	ComponentMask regularMask{0, 0};
	ComponentMask tagMask{0, 0};
	std::array<const void *, MAX_COMPONENTS> copyData{};
	std::array<void *, MAX_COMPONENTS> moveData{};
	copyData.fill(nullptr);
	moveData.fill(nullptr);

	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(([&] {
			 ComponentTypeID id = compIds[I];
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

void ECS::destroyEntity(Entity entity)
{
	if (!alive(entity))
	{
		return;
	}

	// Recursively destroy children first
	destroyHierarchy(entity);

	// Remove from parent's child list
	{
		std::lock_guard<std::mutex> lock(hierarchyMutex_);
		// Get current parent (if any)
		if (entity.index < parent_.size() && parent_[entity.index] != NULL_ENTITY)
		{
			Entity parent = parent_[entity.index];
			// Remove this entity from parent's children list
			if (parent.index < children_.size())
			{
				auto &siblings = children_[parent.index];
				siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());
			}
			parent_[entity.index] = NULL_ENTITY;
		}
		// Clear this entity's own children list (they were already destroyed recursively)
		if (entity.index < children_.size())
		{
			children_[entity.index].clear();
		}
	}

	auto &rec = records_[entity.index];
	Archetype *arch = rec.archetype;
	auto [movedEntity, newSlot] = arch->removeEntity(rec.chunkIndex, rec.slotIndex);

	if (movedEntity.generation != 0)
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

void ECS::destroyHierarchy(Entity entity)
{
	std::vector<Entity> childrenCopy;
	{
		std::lock_guard<std::mutex> lock(hierarchyMutex_);
		if (entity.index < children_.size())
		{
			childrenCopy = children_[entity.index]; // copy
		}
	}
	for (Entity child : childrenCopy)
	{
		destroyEntity(child); // recursive
	}
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
	queryCache_.addArchetype(regularMask, ptr);
	return ptr;
}

void *ECS::getComponentPtr(Entity entity, ComponentTypeID compId)
{
	if (!alive(entity))
	{
		return nullptr;
	}
	auto &rec = records_[entity.index];
	Archetype *arch = rec.archetype;
	ComponentMask mask = arch->getRegularMask();

	bool hasComp;
	if (compId < 64)
	{
		hasComp = (mask.low & (uint64_t(1) << compId)) != 0;
	}
	else
	{
		hasComp = (mask.high & (uint64_t(1) << (compId - 64))) != 0;
	}

	if (!hasComp)
	{
		return nullptr;
	}
	void *arr = arch->getComponentArray(rec.chunkIndex, compId);
	if (!arr)
	{
		return nullptr;
	}
	std::size_t size = ComponentInfos[compId].size;
	return static_cast<std::byte *>(arr) + rec.slotIndex * size;
}

const void *ECS::getComponentPtr(Entity entity, ComponentTypeID compId) const
{
	if (!alive(entity))
	{
		return nullptr;
	}
	const auto &rec = records_[entity.index];
	const Archetype *arch = rec.archetype;
	ComponentMask mask = arch->getRegularMask();
	bool hasComp;
	if (compId < 64)
	{
		hasComp = (mask.low & (uint64_t(1) << compId)) != 0;
	}
	else
	{
		hasComp = (mask.high & (uint64_t(1) << (compId - 64))) != 0;
	}
	if (!hasComp)
	{
		return nullptr;
	}
	const void *arr = arch->getComponentArray(rec.chunkIndex, compId);
	if (!arr)
	{
		return nullptr;
	}
	std::size_t size = ComponentInfos[compId].size;
	return static_cast<const std::byte *>(arr) + rec.slotIndex * size;
}

template <typename T>
void ECS::addComponent(Entity entity, T value)
{
	if (!alive(entity))
	{
		return;
	}
	ComponentTypeID compId = componentId<T>();
	const auto &info = ComponentInfos[compId];
	if (info.isTag)
	{
		auto &rec = records_[entity.index];
		rec.archetype->setTag(rec.chunkIndex, rec.slotIndex, compId);
		return;
	}

	ComponentMask oldRegular = records_[entity.index].archetype->getRegularMask();
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
		// Bump component version
		auto &rec = records_[entity.index];
		rec.archetype->bumpComponentVersion(rec.chunkIndex, compId);
		return;
	}

	std::array<const void *, MAX_COMPONENTS> copyData{};
	std::array<void *, MAX_COMPONENTS> moveData{};
	moveData[compId] = &value;

	moveEntity(entity, newRegular, copyData, moveData);
}

template <typename T>
void ECS::removeComponent(Entity entity)
{
	if (!alive(entity))
	{
		return;
	}
	ComponentTypeID compId = componentId<T>();
	const auto &info = ComponentInfos[compId];
	if (info.isTag)
	{
		auto &rec = records_[entity.index];
		rec.archetype->clearTag(rec.chunkIndex, rec.slotIndex, compId);
		return;
	}

	removeComponent(entity, compId);
}

void ECS::removeComponent(Entity entity, ComponentTypeID compId)
{
	if (!alive(entity))
	{
		return;
	}
	const auto &info = ComponentInfos[compId];
	if (info.isTag)
	{
		auto &rec = records_[entity.index];
		rec.archetype->clearTag(rec.chunkIndex, rec.slotIndex, compId);
		return;
	}

	ComponentMask oldRegular = records_[entity.index].archetype->getRegularMask();

	// Check if present
	bool present;
	if (compId < 64)
	{
		present = (oldRegular.low & (uint64_t(1) << compId)) != 0;
	}
	else
	{
		present = (oldRegular.high & (uint64_t(1) << (compId - 64))) != 0;
	}
	if (!present)
	{
		return;
	}

	ComponentMask newRegular = oldRegular;
	if (compId < 64)
	{
		newRegular.low &= ~(uint64_t(1) << compId);
	}
	else
	{
		newRegular.high &= ~(uint64_t(1) << (compId - 64));
	}

	std::array<const void *, MAX_COMPONENTS> copyData{};
	std::array<void *, MAX_COMPONENTS> moveData{};
	copyData.fill(nullptr);
	moveData.fill(nullptr);
	moveEntity(entity, newRegular, copyData, moveData);
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
	ComponentTypeID tagId = componentId<Tag>();
	if (!ComponentInfos[tagId].isTag)
	{
		return;
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
	ComponentTypeID tagId = componentId<Tag>();
	if (!ComponentInfos[tagId].isTag)
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
	ComponentTypeID tagId = componentId<Tag>();
	if (!ComponentInfos[tagId].isTag)
	{
		return false;
	}
	const auto &rec = records_[entity.index];
	return rec.archetype->hasTag(rec.chunkIndex, rec.slotIndex, tagId);
}

// -----------------------------------------------------------------------------
//  Hierarchy functions (dense vectors)
// -----------------------------------------------------------------------------
void ECS::setParent(Entity child, Entity parent)
{
	if (!alive(child))
	{
		return;
	}
	if (parent != NULL_ENTITY && !alive(parent))
	{
		return;
	}

	std::lock_guard<std::mutex> lock(hierarchyMutex_);

	// Ensure vectors are large enough
	uint32_t maxIdx = std::max(child.index, parent.index);
	if (parent_.size() <= maxIdx)
	{
		parent_.resize(maxIdx + 1, NULL_ENTITY);
		children_.resize(maxIdx + 1);
	}

	// Get current parent
	Entity oldParent = (child.index < parent_.size()) ? parent_[child.index] : NULL_ENTITY;

	if (oldParent == parent)
	{
		return; // no change
	}

	// Remove from old parent's children list
	if (oldParent != NULL_ENTITY && oldParent.index < children_.size())
	{
		auto &siblings = children_[oldParent.index];
		siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
	}

	// Update parent map
	parent_[child.index] = parent;

	// Add to new parent's children list
	if (parent != NULL_ENTITY)
	{
		if (parent.index >= children_.size())
		{
			children_.resize(parent.index + 1);
		}
		children_[parent.index].push_back(child);
	}
}

std::vector<Entity> ECS::getChildren(Entity parent) const
{
	std::lock_guard<std::mutex> lock(hierarchyMutex_);
	if (parent.index < children_.size())
	{
		return children_[parent.index]; // copy
	}
	return {};
}

Entity ECS::getParent(Entity child) const
{
	std::lock_guard<std::mutex> lock(hierarchyMutex_);
	if (child.index < parent_.size())
	{
		return parent_[child.index];
	}
	return NULL_ENTITY;
}

void ECS::moveEntity(Entity entity, ComponentMask newRegularMask, const std::array<const void *, MAX_COMPONENTS> &copyData,
					 const std::array<void *, MAX_COMPONENTS> &moveData, ComponentMask newTags)
{
	auto &rec = records_[entity.index];
	Archetype *srcArch = rec.archetype;

	uint32_t srcChunk = rec.chunkIndex;
	uint32_t srcSlot = rec.slotIndex;

	ComponentMask oldRegular = srcArch->getRegularMask();
	ComponentMask oldTags = srcArch->getTags(srcChunk, srcSlot);

	if (oldRegular == newRegularMask && oldTags == newTags)
	{
		return;
	}

	std::array<const void *, MAX_COMPONENTS> finalCopy{};
	std::array<void *, MAX_COMPONENTS> finalMove{};
	finalCopy.fill(nullptr);
	finalMove.fill(nullptr);

	forEachSetBit(oldRegular, [&](ComponentTypeID id) { finalMove[id] = getComponentPtr(entity, id); });

	ComponentMask moveOverrideMask{0, 0};
	ComponentMask copyOverrideMask{0, 0};

	for (ComponentTypeID id = 0; id < MAX_COMPONENTS; ++id)
	{
		if (moveData[id] != nullptr)
		{
			if (id < 64)
			{
				moveOverrideMask.low |= (uint64_t(1) << id);
			}
			else
			{
				moveOverrideMask.high |= (uint64_t(1) << (id - 64));
			}
		}
		else if (copyData[id] != nullptr)
		{
			if (id < 64)
			{
				copyOverrideMask.low |= (uint64_t(1) << id);
			}
			else
			{
				copyOverrideMask.high |= (uint64_t(1) << (id - 64));
			}
		}
	}

	forEachSetBit(moveOverrideMask, [&](ComponentTypeID id) {
		finalMove[id] = moveData[id];
		finalCopy[id] = nullptr;
	});

	forEachSetBit(copyOverrideMask, [&](ComponentTypeID id) {
		finalCopy[id] = copyData[id];
		finalMove[id] = nullptr;
	});

	Archetype *dstArch = getOrCreateArchetype(newRegularMask);
	auto [newChunk, newSlot] = dstArch->addEntity(entity, finalCopy, finalMove, newTags);
	auto [movedEntity, vacatedSlot] = srcArch->removeEntity(srcChunk, srcSlot);

	rec.archetype = dstArch;
	rec.chunkIndex = newChunk;
	rec.slotIndex = newSlot;

	if (movedEntity.generation != 0)
	{
		auto &movedRec = records_[movedEntity.index];
		movedRec.archetype = srcArch;
		movedRec.chunkIndex = srcChunk;
		movedRec.slotIndex = vacatedSlot;
	}
}

// -----------------------------------------------------------------------------
//  Query implementation helpers
// -----------------------------------------------------------------------------
template <typename... Components>
constexpr ComponentMask build_required_mask()
{
	ComponentMask mask{0, 0};
	(([&] {
		 if constexpr (!is_tag_component<Components>::value)
		 {
			 ComponentTypeID id = componentId<Components>();
			 if (id < 64)
			 {
				 mask.low |= (uint64_t(1) << id);
			 }
			 else
			 {
				 mask.high |= (uint64_t(1) << (id - 64));
			 }
		 }
	 }()),
	 ...);
	return mask;
}

template <typename... Components, typename Func, typename EntityArr, typename CompArrays>
void process_chunk_entities(EntityArr *entityArr, CompArrays &compArrays, uint32_t entityCount, uint32_t chunkIdx, Archetype *arch,
							Func &&func)
{
	for (uint32_t s = 0; s < entityCount; ++s)
	{
		Entity e = entityArr[s];

		bool tagsOk = true;
		ComponentMask entityTags = arch->getTags(chunkIdx, s);
		std::size_t i = 0;
		((tagsOk = tagsOk && (!is_tag_component<Components>::value || (entityTags.low & (uint64_t(1) << componentId<Components>()))), ++i),
		 ...);
		if (!tagsOk)
		{
			continue;
		}

		[&]<std::size_t... Is>(std::index_sequence<Is...>) {
			func(e, ([&]() -> std::conditional_t<is_tag_component<Components>::value,
												 Components,	 // by value
												 Components &> { // by reference
					 if constexpr (is_tag_component<Components>::value)
					 {
						 return Components{};
					 }
					 else
					 {
						 void *ptr
							 = static_cast<std::byte *>(std::get<Is>(compArrays)) + s * ComponentInfos[componentId<Components>()].size;
						 return static_cast<Components &>(*static_cast<Components *>(ptr));
					 }
				 }())...);
		}(std::index_sequence_for<Components...>{});
	}
}

template <typename... Components, typename Func, typename EntityArr, typename CompArrays>
void process_chunk_entities_const(const EntityArr *entityArr, const CompArrays &compArrays, uint32_t entityCount, uint32_t chunkIdx,
								  const Archetype *arch, Func &&func)
{
	for (uint32_t s = 0; s < entityCount; ++s)
	{
		Entity e = entityArr[s];

		bool tagsOk = true;
		ComponentMask entityTags = arch->getTags(chunkIdx, s);
		std::size_t i = 0;
		((tagsOk = tagsOk && (!is_tag_component<Components>::value || (entityTags.low & (uint64_t(1) << componentId<Components>()))), ++i),
		 ...);
		if (!tagsOk)
		{
			continue;
		}

		[&]<std::size_t... Is>(std::index_sequence<Is...>) {
			func(e, ([&]() -> std::conditional_t<is_tag_component<Components>::value, Components, const Components &> {
					 if constexpr (is_tag_component<Components>::value)
					 {
						 return Components{};
					 }
					 else
					 {
						 const void *ptr = static_cast<const std::byte *>(std::get<Is>(compArrays))
										 + s * ComponentInfos[componentId<Components>()].size;
						 return static_cast<const Components &>(*static_cast<const Components *>(ptr));
					 }
				 }())...);
		}(std::index_sequence_for<Components...>{});
	}
}

// -----------------------------------------------------------------------------
//  Sequential forEach (default)
// -----------------------------------------------------------------------------
template <typename... Components, typename Func>
void ECS::forEach(Func &&func)
{
	forEach<Components...>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

template <typename... Components, typename Func>
void ECS::forEach(Func &&func) const
{
	forEach<Components...>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

// -----------------------------------------------------------------------------
//  Parallel forEach with explicit policy
// -----------------------------------------------------------------------------
template <typename... Components, typename Func>
void ECS::forEach(ExecutionPolicy policy, Func &&func)
{
	constexpr ComponentMask requiredRegular = build_required_mask<Components...>();
	const auto &matchingArchetypes = queryCache_.get(requiredRegular);

	if (policy == ExecutionPolicy::Seq)
	{
		// Sequential execution
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
		// Original parallel: one task per chunk using thread pool
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
		// Batched parallel: multiple chunks per task
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

		Latch latch(static_cast<int>((allChunks.size() + 3) / 4)); // Batch size 4
		const std::size_t batchSize = 4;

		for (std::size_t i = 0; i < allChunks.size(); i += batchSize)
		{
			std::size_t end = std::min(i + batchSize, allChunks.size());
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
		// Work stealing with batching
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

		Latch latch(static_cast<int>((allChunks.size() + 7) / 8)); // Batch size 8 for stealing
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

// Const version
template <typename... Components, typename Func>
void ECS::forEach(ExecutionPolicy policy, Func &&func) const
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
		const std::size_t batchSize = 4;

		for (std::size_t i = 0; i < allChunks.size(); i += batchSize)
		{
			std::size_t end = std::min(i + batchSize, allChunks.size());
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

// -----------------------------------------------------------------------------
//  Version-aware forEach with system version tracking
// -----------------------------------------------------------------------------
template <typename... Components, typename Func>
void ECS::forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
{
	constexpr ComponentMask requiredRegular = build_required_mask<Components...>();
	const auto &matchingArchetypes = queryCache_.get(requiredRegular);

	// Collect chunks that need updates
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

	// Process dirty chunks with selected policy
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

			forEachSetBit(requiredRegular, [&](ComponentTypeID id) { version.componentVersions[id] = chunkVer->componentVersions[id]; });
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

		// Update version after all tasks complete
		for (const auto &chunk : dirtyChunks)
		{
			Archetype *arch = std::get<0>(chunk);
			uint32_t c = std::get<1>(chunk);
			const ChunkVersion *chunkVer = std::get<2>(chunk);

			forEachSetBit(requiredRegular, [&](ComponentTypeID id) {
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
		const std::size_t batchSize = 4;

		for (std::size_t i = 0; i < chunks.size(); i += batchSize)
		{
			std::size_t end = std::min(i + batchSize, chunks.size());
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

		// Update versions
		for (const auto &chunk : dirtyChunks)
		{
			Archetype *arch = std::get<0>(chunk);
			uint32_t c = std::get<1>(chunk);
			const ChunkVersion *chunkVer = std::get<2>(chunk);

			forEachSetBit(requiredRegular, [&](ComponentTypeID id) {
				version.componentVersions[id] = std::max(version.componentVersions[id], chunkVer->componentVersions[id]);
			});
			version.version = std::max(version.version, arch->getChunkVersion(c).version);
		}
	}
}

// Const version-aware forEach
template <typename... Components, typename Func>
void ECS::forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
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

	if (policy == ExecutionPolicy::Seq)
	{
		for (const auto &chunk : dirtyChunks)
		{
			Archetype *arch = std::get<0>(chunk);
			uint32_t c = std::get<1>(chunk);
			const ChunkVersion *chunkVer = std::get<2>(chunk);

			processFunc(arch, c);

			forEachSetBit(requiredRegular, [&](ComponentTypeID id) { version.componentVersions[id] = chunkVer->componentVersions[id]; });
		}
		const auto &lastChunk = dirtyChunks.back();
		version.version = std::max(version.version, std::get<0>(lastChunk)->getChunkVersion(std::get<1>(lastChunk)).version);
	}
	else
	{
		// For now, fall back to sequential for const version with version tracking
		for (const auto &chunk : dirtyChunks)
		{
			Archetype *arch = std::get<0>(chunk);
			uint32_t c = std::get<1>(chunk);
			const ChunkVersion *chunkVer = std::get<2>(chunk);

			processFunc(arch, c);

			forEachSetBit(requiredRegular, [&](ComponentTypeID id) { version.componentVersions[id] = chunkVer->componentVersions[id]; });
		}
		const auto &lastChunk = dirtyChunks.back();
		version.version = std::max(version.version, std::get<0>(lastChunk)->getChunkVersion(std::get<1>(lastChunk)).version);
	}
}

// -----------------------------------------------------------------------------
//  forEach with command buffer
// -----------------------------------------------------------------------------
template <typename... Components, typename Func>
void ECS::forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
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
		const std::size_t batchSize = 4;

		for (std::size_t i = 0; i < allChunks.size(); i += batchSize)
		{
			std::size_t end = std::min(i + batchSize, allChunks.size());
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

// -----------------------------------------------------------------------------
//  CommandBuffer method implementations (need ECS to be complete)
// -----------------------------------------------------------------------------
template <typename... Ts>
void CommandBuffer::dispatchAddImpl(ECS &ecs, Entity e, ComponentTypeID id, void *buffer, std::tuple<Ts...>)
{
	bool handled = false;
	(
		[&] {
			if (componentId<Ts>() == id)
			{
				Ts &value = *reinterpret_cast<Ts *>(buffer);
				ecs.addComponent(e, std::move(value));
				handled = true;
			}
		}(),
		...);
	assert(handled && "Unknown component ID in CommandBuffer::apply");
}

void CommandBuffer::dispatchAdd(ECS &ecs, Entity e, ComponentTypeID id, void *buffer)
{
	dispatchAddImpl(ecs, e, id, buffer, ComponentTypes{});
}

void CommandBuffer::dispatchRemove(ECS &ecs, Entity e, ComponentTypeID id)
{
	ecs.removeComponent(e, id);
}

void CommandBuffer::apply(ECS &ecs)
{
	std::vector<std::unique_ptr<ThreadBuffer>> local_buffers;
	{
		std::lock_guard<std::mutex> lock(map_mutex_);
		for (auto &[_, buf] : buffers_)
		{
			local_buffers.push_back(std::move(buf));
		}
		buffers_.clear();
	}
	for (auto &buf : local_buffers)
	{
		for (auto &cmd : buf->commands)
		{
			switch (cmd.type)
			{
				case CmdType::AddComponent:
					dispatchAdd(ecs, cmd.entity, cmd.data.add.compId, cmd.data.add.buffer);
					cmd.destroyBuffer();
					break;
				case CmdType::RemoveComponent:
					dispatchRemove(ecs, cmd.entity, cmd.data.remove.compId);
					break;
				case CmdType::Destroy:
					ecs.destroyEntity(cmd.entity);
					break;
				case CmdType::SetParent:
					ecs.setParent(cmd.entity, cmd.data.setParent.parent);
					break;
			}
		}
	}
}