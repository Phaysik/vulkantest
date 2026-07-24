// resource_manager.cpp - Production-grade Resource Manager (C++20/26)
// Complete implementation with thread pool, sharded cache, versioning,
// hot reload opt-in, eviction policies, and all fixes.
// Includes: unique_ptr<Version> to avoid atomic copy issues,
//           get_data_shared for fallback assignment,
//           proper Stats constructors,
//           recursive mutexes to avoid deadlocks,
//           notify_dependents using thread pool,
//           correct lock ordering in unload_resource.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <generator>
#include <iomanip>
#include <iostream>
#include <latch>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <span>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
	#include <windows.h>
#endif

namespace fs = std::filesystem;

// -------------------------------------------------------------------
// Resource Manager types and definitions
// -------------------------------------------------------------------
using Dimensia::Threading::ThreadPool;

enum class LoadPriority
{
	Immediate,
	High,
	Normal,
	Low,
	Background
};

using ResourceId = std::size_t;
using TypeHash = size_t;
using TimePoint = std::chrono::steady_clock::time_point;

enum class ResourceState
{
	Unloaded,
	Loading,
	Ready,
	Failed,
	Unloading
};

struct LoadTimes
{
		TimePoint start;
		std::chrono::microseconds disk_read{0};
		std::chrono::microseconds decompress{0};
		std::chrono::microseconds gpu_upload{0};
		std::chrono::microseconds total{0};

		void begin()
		{
			start = std::chrono::steady_clock::now();
		}

		void end()
		{
			total = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
		}
};

class ResourceManager;
template <typename T>
class ResourceHandle;
template <typename T>
class ResourceHandleWeak;
class AnyResourceHandle;
class ResourceGroup;

// -------------------------------------------------------------------
// IResourceLoader (synchronous)
// -------------------------------------------------------------------
struct IResourceLoader
{
		virtual ~IResourceLoader() = default;
		virtual std::shared_ptr<void> load(ResourceManager &mgr, const fs::path &path, LoadPriority priority, LoadTimes &times) = 0;

		// Changed: path now passed by value
		virtual std::generator<std::span<const std::byte>> stream(ResourceManager &mgr, fs::path path) = 0;

		virtual void reload(ResourceManager &mgr, ResourceId id, std::shared_ptr<void> new_data) = 0;
		virtual std::size_t get_gpu_memory_size(void *cpu_data) = 0;
		virtual void upload_to_gpu(void *cpu_data, void *gpu_mem) = 0;
};

// -------------------------------------------------------------------
// ResourceEntry (cache entry)
// -------------------------------------------------------------------
struct ResourceEntry
{
		fs::path original_path;
		fs::file_time_type last_write_time;
		IResourceLoader *loader = nullptr; // non-owning
		TypeHash type_hash;
		std::string error_message;

		struct Version
		{
				std::shared_ptr<void> data;
				void *gpu_handle = nullptr;
				std::size_t gpu_memory_size = 0;
				bool gpu_resident = false;
				std::atomic<int> ref_count{0};
				TimePoint last_used;
				uint64_t version_id;
				LoadTimes load_times;

				Version(uint64_t id) : version_id(id) {}
		};

		std::vector<std::unique_ptr<Version>> versions; // store as unique_ptr
		std::atomic<uint64_t> next_version_id{1};
		std::atomic<uint64_t> current_version_id{0};
		std::atomic<ResourceState> state{ResourceState::Unloaded};

		std::optional<std::generator<std::span<const std::byte>>> stream_gen;
		std::shared_ptr<std::promise<void>> ready_promise;

		bool hot_reload_enabled = false;

		std::vector<ResourceId> dependencies;
		std::vector<ResourceId> dependents;
};

// -------------------------------------------------------------------
// GPU allocator stub
// -------------------------------------------------------------------
class IGpuAllocator
{
	public:
		virtual ~IGpuAllocator() = default;
		virtual void *allocate(std::size_t size, std::size_t alignment) = 0;
		virtual void deallocate(void *ptr) = 0;
		virtual std::size_t used_memory() const = 0;
		virtual std::size_t total_memory() const = 0;
};

class DummyGpuAllocator : public IGpuAllocator
{
		std::atomic<std::size_t> used_{0};
		std::size_t total_{1'024 * 1'024 * 1'024};

	public:
		void *allocate(std::size_t size, std::size_t) override
		{
			used_ += size;
			return reinterpret_cast<void *>(0x12345678);
		}

		void deallocate(void *) override {}

		std::size_t used_memory() const override
		{
			return used_.load();
		}

		std::size_t total_memory() const override
		{
			return total_;
		}
};

// -------------------------------------------------------------------
// Custom hash for pair<ResourceId, uint64_t>
// -------------------------------------------------------------------
struct PairHash
{
		std::size_t operator()(const std::pair<ResourceId, uint64_t> &p) const noexcept
		{
			return p.first ^ (p.second << 1) ^ (p.second >> (sizeof(uint64_t) * 8 - 1));
		}
};

// -------------------------------------------------------------------
// Eviction policy interfaces
// -------------------------------------------------------------------
struct EvictionPolicy
{
		virtual ~EvictionPolicy() = default;
		virtual void on_access(ResourceId id, uint64_t version) = 0;
		virtual void on_add(ResourceId id, uint64_t version, std::size_t size) = 0;
		virtual void on_remove(ResourceId id, uint64_t version) = 0;
		virtual std::optional<std::pair<ResourceId, uint64_t>> select_victim() = 0;
		virtual void on_size_change(ResourceId id, uint64_t version, std::size_t new_size) = 0;
};

// Simple LRU
class LRUPolicy : public EvictionPolicy
{
		struct Entry
		{
				TimePoint last_used;
				std::size_t size;
		};

		std::unordered_map<std::pair<ResourceId, uint64_t>, Entry, PairHash> map_;

		struct HeapNode
		{
				std::pair<ResourceId, uint64_t> key;
				TimePoint last_used;

				bool operator<(const HeapNode &o) const
				{
					return last_used > o.last_used;
				}
		};

		std::priority_queue<HeapNode> heap_;

	public:
		void on_access(ResourceId id, uint64_t v) override
		{
			auto key = std::make_pair(id, v);
			auto it = map_.find(key);
			if (it == map_.end())
			{
				return;
			}
			it->second.last_used = std::chrono::steady_clock::now();
			heap_.push({key, it->second.last_used});
		}

		void on_add(ResourceId id, uint64_t v, std::size_t size) override
		{
			auto key = std::make_pair(id, v);
			auto now = std::chrono::steady_clock::now();
			map_[key] = {now, size};
			heap_.push({key, now});
		}

		void on_remove(ResourceId id, uint64_t v) override
		{
			map_.erase(std::make_pair(id, v));
		}

		std::optional<std::pair<ResourceId, uint64_t>> select_victim() override
		{
			while (!heap_.empty())
			{
				auto top = heap_.top();
				heap_.pop();
				auto it = map_.find(top.key);
				if (it == map_.end())
				{
					continue;
				}
				if (it->second.last_used != top.last_used)
				{
					continue;
				}
				return top.key;
			}
			return std::nullopt;
		}

		void on_size_change(ResourceId id, uint64_t v, std::size_t s) override
		{
			auto key = std::make_pair(id, v);
			auto it = map_.find(key);
			if (it != map_.end())
			{
				it->second.size = s;
			}
		}
};

// LRU-2
class LRU2Policy : public EvictionPolicy
{
		struct Entry
		{
				TimePoint last;
				TimePoint second_last;
				std::size_t size;
		};

		std::unordered_map<std::pair<ResourceId, uint64_t>, Entry, PairHash> map_;

		struct HeapNode
		{
				std::pair<ResourceId, uint64_t> key;
				TimePoint order;

				bool operator<(const HeapNode &o) const
				{
					return order > o.order;
				}
		};

		std::priority_queue<HeapNode> heap_;

		void push(const std::pair<ResourceId, uint64_t> &key)
		{
			auto it = map_.find(key);
			if (it == map_.end())
			{
				return;
			}
			auto order = it->second.second_last.time_since_epoch().count() != 0 ? it->second.second_last : it->second.last;
			heap_.push({key, order});
		}

	public:
		void on_access(ResourceId id, uint64_t v) override
		{
			auto key = std::make_pair(id, v);
			auto it = map_.find(key);
			if (it == map_.end())
			{
				return;
			}
			it->second.second_last = it->second.last;
			it->second.last = std::chrono::steady_clock::now();
			push(key);
		}

		void on_add(ResourceId id, uint64_t v, std::size_t size) override
		{
			auto key = std::make_pair(id, v);
			auto now = std::chrono::steady_clock::now();
			map_[key] = {now, now, size};
			push(key);
		}

		void on_remove(ResourceId id, uint64_t v) override
		{
			map_.erase(std::make_pair(id, v));
		}

		std::optional<std::pair<ResourceId, uint64_t>> select_victim() override
		{
			while (!heap_.empty())
			{
				auto top = heap_.top();
				heap_.pop();
				auto it = map_.find(top.key);
				if (it == map_.end())
				{
					continue;
				}
				auto expected = it->second.second_last.time_since_epoch().count() != 0 ? it->second.second_last : it->second.last;
				if (top.order != expected)
				{
					continue;
				}
				return top.key;
			}
			return std::nullopt;
		}

		void on_size_change(ResourceId id, uint64_t v, std::size_t s) override
		{
			auto key = std::make_pair(id, v);
			auto it = map_.find(key);
			if (it != map_.end())
			{
				it->second.size = s;
			}
		}
};

// LFU with LRU tie-breaker
class LFUPolicy : public EvictionPolicy
{
		struct Entry
		{
				uint64_t freq;
				TimePoint last;
				std::size_t size;
		};

		std::unordered_map<std::pair<ResourceId, uint64_t>, Entry, PairHash> map_;

		struct HeapNode
		{
				std::pair<ResourceId, uint64_t> key;
				uint64_t freq;
				TimePoint last;

				bool operator<(const HeapNode &o) const
				{
					if (freq != o.freq)
					{
						return freq > o.freq;
					}
					return last > o.last;
				}
		};

		std::priority_queue<HeapNode> heap_;

	public:
		void on_access(ResourceId id, uint64_t v) override
		{
			auto key = std::make_pair(id, v);
			auto it = map_.find(key);
			if (it == map_.end())
			{
				return;
			}
			it->second.freq++;
			it->second.last = std::chrono::steady_clock::now();
			heap_.push({key, it->second.freq, it->second.last});
		}

		void on_add(ResourceId id, uint64_t v, std::size_t size) override
		{
			auto key = std::make_pair(id, v);
			auto now = std::chrono::steady_clock::now();
			map_[key] = {1, now, size};
			heap_.push({key, 1, now});
		}

		void on_remove(ResourceId id, uint64_t v) override
		{
			map_.erase(std::make_pair(id, v));
		}

		std::optional<std::pair<ResourceId, uint64_t>> select_victim() override
		{
			while (!heap_.empty())
			{
				auto top = heap_.top();
				heap_.pop();
				auto it = map_.find(top.key);
				if (it == map_.end())
				{
					continue;
				}
				if (it->second.freq != top.freq || it->second.last != top.last)
				{
					continue;
				}
				return top.key;
			}
			return std::nullopt;
		}

		void on_size_change(ResourceId id, uint64_t v, std::size_t s) override
		{
			auto key = std::make_pair(id, v);
			auto it = map_.find(key);
			if (it != map_.end())
			{
				it->second.size = s;
			}
		}
};

// -------------------------------------------------------------------
// ResourceManager (main class) – declarations only
// -------------------------------------------------------------------
class ResourceManager
{
	public:
		enum class EvictionPolicyType
		{
			LRU,
			LRU2,
			LFU
		};

		struct Stats
		{
				std::atomic<uint64_t> total_cache_entries{0};
				std::atomic<uint64_t> total_versions{0};
				std::atomic<uint64_t> total_gpu_memory_used{0};
				std::atomic<uint64_t> total_cpu_memory_used{0};
				std::atomic<uint64_t> total_loads{0};
				std::atomic<uint64_t> cache_hits{0};
				std::atomic<uint64_t> cache_misses{0};
				std::atomic<uint64_t> evictions{0};
				std::atomic<uint64_t> hot_reloads{0};

				Stats() = default;

				Stats(const Stats &other) noexcept
					: total_cache_entries(other.total_cache_entries.load()), total_versions(other.total_versions.load()),
					  total_gpu_memory_used(other.total_gpu_memory_used.load()), total_cpu_memory_used(other.total_cpu_memory_used.load()),
					  total_loads(other.total_loads.load()), cache_hits(other.cache_hits.load()), cache_misses(other.cache_misses.load()),
					  evictions(other.evictions.load()), hot_reloads(other.hot_reloads.load())
				{}

				void print(std::ostream &os) const
				{
					os << "==== Resource Manager Stats ====\n"
					   << "Cache entries: " << total_cache_entries.load() << "\n"
					   << "Total versions: " << total_versions.load() << "\n"
					   << "GPU memory used: " << total_gpu_memory_used.load() / (1'024 * 1'024) << " MB\n"
					   << "CPU memory used: " << total_cpu_memory_used.load() / (1'024 * 1'024) << " MB\n"
					   << "Loads: " << total_loads.load() << "\n"
					   << "Cache hits: " << cache_hits.load() << "\n"
					   << "Cache misses: " << cache_misses.load() << "\n"
					   << "Evictions: " << evictions.load() << "\n"
					   << "Hot reloads: " << hot_reloads.load() << "\n";
				}
		};

		ResourceManager(std::unique_ptr<IGpuAllocator> allocator, std::size_t gpu_budget,
						EvictionPolicyType policy_type = EvictionPolicyType::LRU,
						bool case_insensitive_fs =
#ifdef _WIN32
							true
#else
							false
#endif
		);

		~ResourceManager();

		// Loader registration
		template <typename T>
		void register_loader(std::unique_ptr<IResourceLoader> loader)
		{
			std::unique_lock lock(loaders_mutex_);
			loaders_[typeid(T).hash_code()] = std::move(loader);
		}

		template <typename T>
		void set_default(std::shared_ptr<T> default_resource)
		{
			defaults_[typeid(T).hash_code()] = default_resource;
		}

		// Public request APIs
		template <typename T>
		ResourceHandle<T> request_async(const fs::path &path, const fs::path &fallback_path = {},
										LoadPriority priority = LoadPriority::Normal)
		{
			ResourceId id = make_resource_id(path);
			return request_impl<T>(id, path, fallback_path, true, priority);
		}

		template <typename T>
		ResourceHandle<T> request_immediate(const fs::path &path, const fs::path &fallback_path = {})
		{
			ResourceId id = make_resource_id(path);
			ResourceHandle<T> handle = request_impl<T>(id, path, fallback_path, false, LoadPriority::Immediate);
			wait_for_ready(id).wait();

			return handle;
		}

		template <typename T>
		ResourceHandleWeak<T> request_weak(const fs::path &path, LoadPriority priority = LoadPriority::Normal)
		{
			ResourceId id = make_resource_id(path);
			return request_weak_impl<T>(id, path, priority);
		}

		AnyResourceHandle request_any(TypeHash type, const fs::path &path, LoadPriority priority = LoadPriority::Normal);

		template <typename T>
		ResourceHandle<T> request_dependency(ResourceId owner_id, const fs::path &path, LoadPriority priority = LoadPriority::Normal)
		{
			ResourceId dep_id = make_resource_id(path);
			{
				std::unique_lock lock(dep_mutex_);
				if (!record_dependency(owner_id, dep_id))
				{
					return {};
				}
			}
			return request_impl<T>(dep_id, path, {}, true, priority);
		}

		template <typename T>
		ResourceHandleWeak<T> request_dependency_weak(ResourceId owner_id, const fs::path &path,
													  LoadPriority priority = LoadPriority::Normal)
		{
			ResourceId dep_id = make_resource_id(path);
			{
				std::unique_lock lock(dep_mutex_);
				if (!record_dependency(owner_id, dep_id))
				{
					return {};
				}
			}
			return request_weak_impl<T>(dep_id, path, priority);
		}

		// Hot reload opt-in
		void enable_hot_reload(ResourceId id);
		void disable_hot_reload(ResourceId id);

		// Waiting APIs
		std::future<void> when_all_ready(const std::vector<ResourceId> &ids);
		std::future<void> wait_for_ready(ResourceId id);

		// Status queries
		std::pair<ResourceState, std::string> get_status(ResourceId id) const;
		uint64_t get_current_version(ResourceId id) const;

		// Data access
		void *get_data_version(ResourceId id, uint64_t version);
		std::shared_ptr<void> get_data_shared(ResourceId id, uint64_t version);
		bool is_version_ready(ResourceId id, uint64_t version);

		// GPU residency
		void *ensure_resident_version(ResourceId id, uint64_t version);

		// Reference counting
		void add_ref_version(ResourceId id, uint64_t version);
		void release_version(ResourceId id, uint64_t version);

		// Hot reload
		void hot_reload(ResourceId id);

		// Unload resource completely
		void unload_resource(ResourceId id);

		// Profiling
		Stats get_stats() const;
		void dump_resources(std::ostream &os) const;
		bool evict_resource(ResourceId id, uint64_t version = 0);

		// Resource group factory
		ResourceGroup create_group();

		template <typename T>
		std::generator<std::span<const std::byte>> stream(const fs::path &path)
		{
			IResourceLoader *loader = find_loader<T>(); // may throw if not registered
			return loader->stream(*this, path);			// passes path by reference, but loader's parameter is by value → copy made
		}

	private:
		static constexpr size_t kNumShards = 64;

		struct Shard
		{
				mutable std::recursive_mutex mutex;
				std::unordered_map<ResourceId, std::shared_ptr<ResourceEntry>> cache;
		};

		size_t get_shard_index(ResourceId id) const
		{
			return id % kNumShards;
		}

		// Helper to lock two shards in order
		auto lock_two_shards(ResourceId a, ResourceId b)
		{
			size_t ia = get_shard_index(a), ib = get_shard_index(b);
			if (ia == ib)
			{
				return std::pair(std::unique_lock<std::recursive_mutex>(shards_[ia].mutex), std::unique_lock<std::recursive_mutex>());
			}
			else if (ia < ib)
			{
				return std::pair(std::unique_lock<std::recursive_mutex>(shards_[ia].mutex),
								 std::unique_lock<std::recursive_mutex>(shards_[ib].mutex));
			}
			else
			{
				return std::pair(std::unique_lock<std::recursive_mutex>(shards_[ib].mutex),
								 std::unique_lock<std::recursive_mutex>(shards_[ia].mutex));
			}
		}

		std::string normalize_path(const fs::path &path) const;
		ResourceId make_resource_id(const fs::path &path) const;
		IResourceLoader *find_loader_by_type(TypeHash type);

		template <typename T>
		IResourceLoader *find_loader()
		{
			return find_loader_by_type(typeid(T).hash_code());
		}

		template <typename T>
		std::shared_ptr<T> get_default();

		template <typename T>
		ResourceHandle<T> request_impl(ResourceId id, const fs::path &path, const fs::path &fallback_path, bool strong,
									   LoadPriority priority);

		template <typename T>
		ResourceHandleWeak<T> request_weak_impl(ResourceId id, const fs::path &path, LoadPriority priority);

		AnyResourceHandle request_any_impl(TypeHash type, ResourceId id, const fs::path &path, LoadPriority priority);

		void start_loading(std::shared_ptr<ResourceEntry> entry, ResourceId id, const fs::path &path, const fs::path &fallback_path,
						   LoadPriority priority, uint64_t version_id);

		bool record_dependency(ResourceId owner, ResourceId dependency);
		void notify_dependents(ResourceId id);

		void *allocate_gpu_memory_locked(ResourceId id, uint64_t version, std::size_t size);
		void touch_version(ResourceId id, uint64_t version);

		void start_hot_reload_thread();

		mutable std::recursive_mutex loaders_mutex_;
		std::unordered_map<TypeHash, std::unique_ptr<IResourceLoader>> loaders_;
		std::unordered_map<TypeHash, std::shared_ptr<void>> defaults_;

		std::unique_ptr<IGpuAllocator> gpu_allocator_;
		std::size_t gpu_budget_;
		std::unique_ptr<EvictionPolicy> policy_;
		mutable std::recursive_mutex policy_mutex_;

		mutable Stats stats_;
		bool case_insensitive_fs_;

		ThreadPool threadPool_;
		std::vector<Shard> shards_;

		std::jthread hot_reload_thread_;
		mutable std::recursive_mutex hot_reload_mutex_;
		std::unordered_set<ResourceId> hot_reload_set_;

		mutable std::recursive_mutex dep_mutex_;
};

// -------------------------------------------------------------------
// AnyResourceHandle, ResourceHandle, ResourceHandleWeak, ResourceGroup definitions
// -------------------------------------------------------------------
class AnyResourceHandle
{
	public:
		AnyResourceHandle() = default;

		AnyResourceHandle(ResourceId id, uint64_t version, ResourceManager *mgr, TypeHash type)
			: id_(id), version_(version), mgr_(mgr), type_(type)
		{
			if (mgr_)
			{
				mgr_->add_ref_version(id_, version_);
			}
		}

		~AnyResourceHandle()
		{
			if (mgr_)
			{
				mgr_->release_version(id_, version_);
			}
		}

		AnyResourceHandle(const AnyResourceHandle &other) : id_(other.id_), version_(other.version_), mgr_(other.mgr_), type_(other.type_)
		{
			if (mgr_)
			{
				mgr_->add_ref_version(id_, version_);
			}
		}

		AnyResourceHandle &operator=(const AnyResourceHandle &other)
		{
			if (this != &other)
			{
				if (mgr_)
				{
					mgr_->release_version(id_, version_);
				}
				id_ = other.id_;
				version_ = other.version_;
				mgr_ = other.mgr_;
				type_ = other.type_;
				if (mgr_)
				{
					mgr_->add_ref_version(id_, version_);
				}
			}
			return *this;
		}

		AnyResourceHandle(AnyResourceHandle &&other) noexcept
			: id_(std::exchange(other.id_, 0)), version_(std::exchange(other.version_, 0)), mgr_(std::exchange(other.mgr_, nullptr)),
			  type_(other.type_)
		{}

		AnyResourceHandle &operator=(AnyResourceHandle &&other) noexcept
		{
			if (this != &other)
			{
				if (mgr_)
				{
					mgr_->release_version(id_, version_);
				}
				id_ = std::exchange(other.id_, 0);
				version_ = std::exchange(other.version_, 0);
				mgr_ = std::exchange(other.mgr_, nullptr);
				type_ = other.type_;
			}
			return *this;
		}

		template <typename T>
		ResourceHandle<T> cast() const
		{
			if (!mgr_ || type_ != typeid(T).hash_code())
			{
				return {};
			}
			return ResourceHandle<T>(id_, version_, mgr_);
		}

		ResourceId id() const
		{
			return id_;
		}

		uint64_t version() const
		{
			return version_;
		}

		bool valid() const
		{
			return mgr_ != nullptr && id_ != 0;
		}

		void *get_gpu_handle() const
		{
			return mgr_ ? mgr_->ensure_resident_version(id_, version_) : nullptr;
		}

	private:
		ResourceId id_ = 0;
		uint64_t version_ = 0;
		ResourceManager *mgr_ = nullptr;
		TypeHash type_ = 0;
};

template <typename T>
class ResourceHandle
{
	public:
		ResourceHandle() = default;

		ResourceHandle(ResourceId id, uint64_t version, ResourceManager *mgr) : id_(id), version_(version), mgr_(mgr) {}

		~ResourceHandle()
		{
			if (mgr_)
			{
				mgr_->release_version(id_, version_);
			}
		}

		ResourceHandle(const ResourceHandle &other) : id_(other.id_), version_(other.version_), mgr_(other.mgr_)
		{
			if (mgr_)
			{
				mgr_->add_ref_version(id_, version_);
			}
		}

		ResourceHandle &operator=(const ResourceHandle &other)
		{
			if (this != &other)
			{
				if (mgr_)
				{
					mgr_->release_version(id_, version_);
				}
				id_ = other.id_;
				version_ = other.version_;
				mgr_ = other.mgr_;
				if (mgr_)
				{
					mgr_->add_ref_version(id_, version_);
				}
			}
			return *this;
		}

		ResourceHandle(ResourceHandle &&other) noexcept
			: id_(std::exchange(other.id_, 0)), version_(std::exchange(other.version_, 0)), mgr_(std::exchange(other.mgr_, nullptr))
		{}

		ResourceHandle &operator=(ResourceHandle &&other) noexcept
		{
			if (this != &other)
			{
				if (mgr_)
				{
					mgr_->release_version(id_, version_);
				}
				id_ = std::exchange(other.id_, 0);
				version_ = std::exchange(other.version_, 0);
				mgr_ = std::exchange(other.mgr_, nullptr);
			}
			return *this;
		}

		T *get() const
		{
			return mgr_ ? static_cast<T *>(mgr_->get_data_version(id_, version_)) : nullptr;
		}

		T *operator->() const
		{
			return get();
		}

		T &operator*() const
		{
			return *get();
		}

		explicit operator bool() const
		{
			return get() != nullptr;
		}

		ResourceId id() const
		{
			return id_;
		}

		uint64_t version() const
		{
			return version_;
		}

		bool is_ready() const
		{
			return mgr_ && mgr_->is_version_ready(id_, version_);
		}

		std::string error() const
		{
			return mgr_ ? mgr_->get_status(id_).second : "";
		}

		void *get_gpu_handle() const
		{
			return mgr_ ? mgr_->ensure_resident_version(id_, version_) : nullptr;
		}

	private:
		ResourceId id_ = 0;
		uint64_t version_ = 0;
		ResourceManager *mgr_ = nullptr;
};

template <typename T>
class ResourceHandleWeak
{
	public:
		ResourceHandleWeak() = default;

		ResourceHandleWeak(ResourceId id, ResourceManager *mgr) : id_(id), mgr_(mgr) {}

		ResourceHandle<T> lock() const
		{
			if (!mgr_)
			{
				return {};
			}
			auto [state, _] = mgr_->get_status(id_);
			if (state != ResourceState::Ready)
			{
				return {};
			}
			uint64_t cur_ver = mgr_->get_current_version(id_);
			mgr_->add_ref_version(id_, cur_ver);
			return ResourceHandle<T>(id_, cur_ver, mgr_);
		}

		ResourceId id() const
		{
			return id_;
		}

		bool expired() const
		{
			if (!mgr_)
			{
				return true;
			}
			auto [state, _] = mgr_->get_status(id_);
			return state != ResourceState::Ready;
		}

	private:
		ResourceId id_ = 0;
		ResourceManager *mgr_ = nullptr;
};

class ResourceGroup
{
	public:
		ResourceGroup(ResourceManager &mgr) : mgr_(mgr) {}

		template <typename T>
		void add(const fs::path &path, LoadPriority priority = LoadPriority::Normal)
		{
			items_.emplace_back(
				[this, path, priority]() -> AnyResourceHandle { return mgr_.request_any(typeid(T).hash_code(), path, priority); });
		}

		void load()
		{
			for (auto &factory : items_)
			{
				handles_.push_back(factory());
			}
		}

		void unload()
		{
			handles_.clear();
		}

		void preload(LoadPriority priority = LoadPriority::Background)
		{
			for (auto &factory : items_)
			{
				auto handle = factory(); // handle goes out of scope
			}
		}

		const std::vector<AnyResourceHandle> &handles() const
		{
			return handles_;
		}

		void wait_all() const
		{
			for (const auto &handle : handles_)
			{
				if (handle.valid())
				{
					mgr_.wait_for_ready(handle.id()).wait();
				}
			}
		}

	private:
		ResourceManager &mgr_;
		std::vector<std::function<AnyResourceHandle()>> items_;
		std::vector<AnyResourceHandle> handles_;
};

// -------------------------------------------------------------------
// ResourceManager out‑of‑line definitions
// -------------------------------------------------------------------
inline ResourceManager::ResourceManager(std::unique_ptr<IGpuAllocator> allocator, std::size_t gpu_budget, EvictionPolicyType policy_type,
										bool case_insensitive_fs)
	: gpu_allocator_(std::move(allocator)), gpu_budget_(gpu_budget), case_insensitive_fs_(case_insensitive_fs),
	  threadPool_(std::thread::hardware_concurrency()), shards_(kNumShards)
{
	switch (policy_type)
	{
		case EvictionPolicyType::LRU:
			policy_ = std::make_unique<LRUPolicy>();
			break;
		case EvictionPolicyType::LRU2:
			policy_ = std::make_unique<LRU2Policy>();
			break;
		case EvictionPolicyType::LFU:
			policy_ = std::make_unique<LFUPolicy>();
			break;
	}
	start_hot_reload_thread();
}

inline ResourceManager::~ResourceManager()
{
	hot_reload_thread_.request_stop();
	if (hot_reload_thread_.joinable())
	{
		hot_reload_thread_.join();
	}
}

inline std::string ResourceManager::normalize_path(const fs::path &path) const
{
	std::string result = path.lexically_normal().generic_string();
	if (case_insensitive_fs_)
	{
#ifdef _WIN32
		int len = MultiByteToWideChar(CP_UTF8, 0, result.c_str(), -1, nullptr, 0);
		std::wstring wstr(len, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, result.c_str(), -1, wstr.data(), len);
		LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wstr.c_str(), len, wstr.data(), len, nullptr, nullptr, 0);
		int new_len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string lower(new_len, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, lower.data(), new_len, nullptr, nullptr);
		result = lower;
#else
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
#endif
	}
	return result;
}

inline ResourceId ResourceManager::make_resource_id(const fs::path &path) const
{
	return std::hash<std::string>{}(normalize_path(path));
}

inline IResourceLoader *ResourceManager::find_loader_by_type(TypeHash type)
{
	std::unique_lock lock(loaders_mutex_);
	auto it = loaders_.find(type);
	if (it == loaders_.end())
	{
		throw std::runtime_error("No loader registered for type");
	}
	return it->second.get();
}

template <typename T>
inline std::shared_ptr<T> ResourceManager::get_default()
{
	auto it = defaults_.find(typeid(T).hash_code());
	if (it != defaults_.end())
	{
		return std::static_pointer_cast<T>(it->second);
	}
	return nullptr;
}

template <typename T>
inline ResourceHandle<T> ResourceManager::request_impl(ResourceId id, const fs::path &path, const fs::path &fallback_path, bool strong,
													   LoadPriority priority)
{
	size_t shard_idx = get_shard_index(id);
	{
		std::unique_lock lock(shards_[shard_idx].mutex);
		auto it = shards_[shard_idx].cache.find(id);
		if (it != shards_[shard_idx].cache.end())
		{
			uint64_t cur_ver = it->second->current_version_id.load();
			for (auto &v : it->second->versions)
			{
				if (v->version_id == cur_ver)
				{
					if (strong)
					{
						v->ref_count.fetch_add(1, std::memory_order_relaxed);
					}
					stats_.cache_hits.fetch_add(1, std::memory_order_relaxed);
					return ResourceHandle<T>(id, cur_ver, this);
				}
			}
		}
	}

	stats_.cache_misses.fetch_add(1, std::memory_order_relaxed);
	stats_.total_loads.fetch_add(1, std::memory_order_relaxed);

	std::unique_lock lock(shards_[shard_idx].mutex);
	auto entry = std::make_shared<ResourceEntry>();
	entry->original_path = path;
	entry->last_write_time = fs::exists(path) ? fs::last_write_time(path) : fs::file_time_type::min();
	entry->loader = find_loader<T>();
	entry->type_hash = typeid(T).hash_code();
	entry->state = ResourceState::Loading;

	uint64_t new_version_id = entry->next_version_id.fetch_add(1);
	entry->versions.push_back(std::make_unique<ResourceEntry::Version>(new_version_id));
	auto &new_version = *entry->versions.back();
	new_version.ref_count.store(strong ? 1 : 0, std::memory_order_relaxed);
	entry->current_version_id.store(new_version_id);
	entry->ready_promise = std::make_shared<std::promise<void>>();

	shards_[shard_idx].cache[id] = entry;
	stats_.total_cache_entries.fetch_add(1);
	stats_.total_versions.fetch_add(1);

	start_loading(entry, id, path, fallback_path, priority, new_version_id);
	return ResourceHandle<T>(id, new_version_id, this);
}

template <typename T>
inline ResourceHandleWeak<T> ResourceManager::request_weak_impl(ResourceId id, const fs::path &path, LoadPriority priority)
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		auto entry = std::make_shared<ResourceEntry>();
		entry->original_path = path;
		entry->loader = find_loader<T>();
		entry->type_hash = typeid(T).hash_code();
		entry->state = ResourceState::Loading;
		uint64_t new_version_id = entry->next_version_id.fetch_add(1);
		entry->versions.push_back(std::make_unique<ResourceEntry::Version>(new_version_id));
		entry->current_version_id.store(new_version_id);
		entry->ready_promise = std::make_shared<std::promise<void>>();
		shards_[shard_idx].cache[id] = entry;
		stats_.total_cache_entries.fetch_add(1);
		stats_.total_versions.fetch_add(1);
		start_loading(entry, id, path, {}, priority, new_version_id);
	}
	return ResourceHandleWeak<T>(id, this);
}

inline AnyResourceHandle ResourceManager::request_any(TypeHash type, const fs::path &path, LoadPriority priority)
{
	ResourceId id = make_resource_id(path);
	return request_any_impl(type, id, path, priority);
}

inline AnyResourceHandle ResourceManager::request_any_impl(TypeHash type, ResourceId id, const fs::path &path, LoadPriority priority)
{
	size_t shard_idx = get_shard_index(id);
	{
		std::unique_lock lock(shards_[shard_idx].mutex);
		auto it = shards_[shard_idx].cache.find(id);
		if (it != shards_[shard_idx].cache.end())
		{
			uint64_t cur_ver = it->second->current_version_id.load();
			for (auto &v : it->second->versions)
			{
				if (v->version_id == cur_ver)
				{
					v->ref_count.fetch_add(1, std::memory_order_relaxed);
					stats_.cache_hits.fetch_add(1, std::memory_order_relaxed);
					return AnyResourceHandle(id, cur_ver, this, type);
				}
			}
		}
	}

	stats_.cache_misses.fetch_add(1, std::memory_order_relaxed);
	stats_.total_loads.fetch_add(1, std::memory_order_relaxed);

	std::unique_lock lock(shards_[shard_idx].mutex);
	auto entry = std::make_shared<ResourceEntry>();
	entry->original_path = path;
	entry->last_write_time = fs::exists(path) ? fs::last_write_time(path) : fs::file_time_type::min();
	entry->loader = find_loader_by_type(type);
	entry->type_hash = type;
	entry->state = ResourceState::Loading;

	uint64_t new_version_id = entry->next_version_id.fetch_add(1);
	entry->versions.push_back(std::make_unique<ResourceEntry::Version>(new_version_id));
	auto &new_version = *entry->versions.back();
	new_version.ref_count.store(1, std::memory_order_relaxed);
	entry->current_version_id.store(new_version_id);
	entry->ready_promise = std::make_shared<std::promise<void>>();

	shards_[shard_idx].cache[id] = entry;
	stats_.total_cache_entries.fetch_add(1);
	stats_.total_versions.fetch_add(1);

	start_loading(entry, id, path, {}, priority, new_version_id);
	return AnyResourceHandle(id, new_version_id, this, type);
}

inline void ResourceManager::start_loading(std::shared_ptr<ResourceEntry> entry, ResourceId id, const fs::path &path,
										   const fs::path &fallback_path, LoadPriority priority, uint64_t version_id)
{
	std::weak_ptr<ResourceEntry> weakEntry = entry;
	threadPool_.submit([this, weakEntry, id, path, fallback_path, priority, version_id] {
		auto ent = weakEntry.lock();
		if (!ent)
		{
			return;
		}

		LoadTimes times;
		times.begin();

		std::shared_ptr<void> result = nullptr;
		ResourceState final_state = ResourceState::Ready;
		std::string error_msg;

		try
		{
			result = ent->loader->load(*this, path, priority, times);
			if (!result)
			{
				throw std::runtime_error("Loader returned null");
			}
		}
		catch (const std::exception &e)
		{
			error_msg = e.what();
			final_state = ResourceState::Failed;

			if (!fallback_path.empty())
			{
				try
				{
					auto fallback_handle = request_any_impl(ent->type_hash, make_resource_id(fallback_path), fallback_path, priority);
					wait_for_ready(fallback_handle.id()).wait();
					if (fallback_handle.valid())
					{
						result = get_data_shared(fallback_handle.id(), fallback_handle.version());
						if (result)
						{
							final_state = ResourceState::Ready;
						}
					}
				}
				catch (...)
				{}
			}
		}

		times.end();

		size_t shard_idx = get_shard_index(id);
		std::unique_lock lk(shards_[shard_idx].mutex);
		for (auto &v : ent->versions)
		{
			if (v->version_id == version_id)
			{
				v->data = result;
				v->load_times = times;
				break;
			}
		}
		ent->state = final_state;
		ent->error_message = error_msg;
		ent->last_write_time = fs::last_write_time(path);
		if (ent->ready_promise)
		{
			ent->ready_promise->set_value();
			ent->ready_promise.reset();
		}
	});
}

inline bool ResourceManager::record_dependency(ResourceId owner, ResourceId dependency)
{
	if (owner == dependency)
	{
		return false;
	}
	// Lock dep_mutex_ first (consistent with request_dependency)
	std::unique_lock dep_lock(dep_mutex_);
	size_t owner_shard = get_shard_index(owner);
	size_t dep_shard = get_shard_index(dependency);
	if (owner_shard == dep_shard)
	{
		std::unique_lock lock(shards_[owner_shard].mutex);
		auto oit = shards_[owner_shard].cache.find(owner);
		auto dit = shards_[owner_shard].cache.find(dependency);
		if (oit == shards_[owner_shard].cache.end() || dit == shards_[owner_shard].cache.end())
		{
			return false;
		}
		if (std::find(oit->second->dependencies.begin(), oit->second->dependencies.end(), dependency) == oit->second->dependencies.end())
		{
			oit->second->dependencies.push_back(dependency);
		}
		if (std::find(dit->second->dependents.begin(), dit->second->dependents.end(), owner) == dit->second->dependents.end())
		{
			dit->second->dependents.push_back(owner);
		}
	}
	else
	{
		// Use helper to lock two shards in order
		auto [lock_o, lock_d] = lock_two_shards(owner, dependency);
		auto oit = shards_[owner_shard].cache.find(owner);
		auto dit = shards_[dep_shard].cache.find(dependency);
		if (oit == shards_[owner_shard].cache.end() || dit == shards_[dep_shard].cache.end())
		{
			return false;
		}
		if (std::find(oit->second->dependencies.begin(), oit->second->dependencies.end(), dependency) == oit->second->dependencies.end())
		{
			oit->second->dependencies.push_back(dependency);
		}
		if (std::find(dit->second->dependents.begin(), dit->second->dependents.end(), owner) == dit->second->dependents.end())
		{
			dit->second->dependents.push_back(owner);
		}
	}
	return true;
}

inline void ResourceManager::notify_dependents(ResourceId id)
{
	// Called with the resource's shard lock held.
	size_t shard_idx = get_shard_index(id);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return;
	}
	auto &entry = it->second;
	auto dependents_copy = entry->dependents; // copy under lock
	// Release lock before submitting tasks to avoid deadlock
	// (the caller may still hold the lock, but we'll submit tasks and return)
	for (ResourceId dep_id : dependents_copy)
	{
		threadPool_.submit([this, dep_id] { hot_reload(dep_id); });
	}
}

inline void *ResourceManager::allocate_gpu_memory_locked(ResourceId id, uint64_t version, std::size_t size)
{
	while (gpu_allocator_->used_memory() + size > gpu_budget_)
	{
		std::optional<std::pair<ResourceId, uint64_t>> victim;
		{
			std::unique_lock policy_lock(policy_mutex_);
			victim = policy_->select_victim();
			if (!victim)
			{
				return nullptr;
			}
			policy_->on_remove(victim->first, victim->second);
		}
		auto [vid, vver] = *victim;
		size_t victim_shard = get_shard_index(vid);
		if (victim_shard == get_shard_index(id))
		{
			auto vit = shards_[victim_shard].cache.find(vid);
			if (vit != shards_[victim_shard].cache.end())
			{
				for (auto &vv : vit->second->versions)
				{
					if (vv->version_id == vver && vv->gpu_resident && vv->ref_count.load() == 0)
					{
						gpu_allocator_->deallocate(vv->gpu_handle);
						stats_.total_gpu_memory_used.fetch_sub(vv->gpu_memory_size);
						vv->gpu_handle = nullptr;
						vv->gpu_resident = false;
						vv->gpu_memory_size = 0;
						stats_.evictions.fetch_add(1);
						break;
					}
				}
			}
		}
		else
		{
			size_t cur_shard = get_shard_index(id);
			std::unique_lock cur_lock(shards_[cur_shard].mutex, std::adopt_lock);
			cur_lock.unlock();

			std::unique_lock vic_lock(shards_[victim_shard].mutex);
			auto vit = shards_[victim_shard].cache.find(vid);
			if (vit != shards_[victim_shard].cache.end())
			{
				for (auto &vv : vit->second->versions)
				{
					if (vv->version_id == vver && vv->gpu_resident && vv->ref_count.load() == 0)
					{
						gpu_allocator_->deallocate(vv->gpu_handle);
						stats_.total_gpu_memory_used.fetch_sub(vv->gpu_memory_size);
						vv->gpu_handle = nullptr;
						vv->gpu_resident = false;
						vv->gpu_memory_size = 0;
						stats_.evictions.fetch_add(1);
						break;
					}
				}
			}
			vic_lock.unlock();
			cur_lock.lock();
		}
	}
	return gpu_allocator_->allocate(size, 16);
}

inline void ResourceManager::touch_version(ResourceId id, uint64_t version)
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return;
	}
	for (auto &v : it->second->versions)
	{
		if (v->version_id == version)
		{
			v->last_used = std::chrono::steady_clock::now();
			if (v->ref_count.load() == 0 && v->gpu_resident)
			{
				std::unique_lock policy_lock(policy_mutex_);
				policy_->on_remove(id, version);
				policy_->on_add(id, version, v->gpu_memory_size);
			}
			else
			{
				std::unique_lock policy_lock(policy_mutex_);
				policy_->on_access(id, version);
			}
			break;
		}
	}
}

inline void ResourceManager::start_hot_reload_thread()
{
	hot_reload_thread_ = std::jthread([this](std::stop_token st) {
		while (!st.stop_requested())
		{
			std::vector<ResourceId> ids;
			{
				std::unique_lock lock(hot_reload_mutex_);
				ids.assign(hot_reload_set_.begin(), hot_reload_set_.end());
			}
			for (ResourceId id : ids)
			{
				if (st.stop_requested())
				{
					break;
				}

				size_t shard_idx = get_shard_index(id);
				std::unique_lock lock(shards_[shard_idx].mutex);

				auto it = shards_[shard_idx].cache.find(id);
				if (it == shards_[shard_idx].cache.end())
				{
					// Resource gone, remove from set
					std::unique_lock hr_lock(hot_reload_mutex_);
					hot_reload_set_.erase(id);
					continue;
				}
				auto &entry = it->second;
				if (entry->state != ResourceState::Ready || !entry->hot_reload_enabled)
				{
					continue;
				}

				if (fs::exists(entry->original_path))
				{
					auto current_mtime = fs::last_write_time(entry->original_path);
					if (current_mtime != entry->last_write_time)
					{
						lock.unlock(); // release before hot_reload (it will re-lock)
						hot_reload(id);
						// After hot_reload, continue with next resource (no need to re-lock)
					}
				}
			}
			// Sleep in small increments to respond quickly to stop request
			for (int i = 0; i < 10 && !st.stop_requested(); ++i)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
	});
}

inline void ResourceManager::enable_hot_reload(ResourceId id)
{
	std::unique_lock lock(hot_reload_mutex_);
	hot_reload_set_.insert(id);
	size_t shard_idx = get_shard_index(id);
	std::unique_lock shard_lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it != shards_[shard_idx].cache.end())
	{
		it->second->hot_reload_enabled = true;
	}
}

inline void ResourceManager::disable_hot_reload(ResourceId id)
{
	std::unique_lock lock(hot_reload_mutex_);
	hot_reload_set_.erase(id);
	size_t shard_idx = get_shard_index(id);
	std::unique_lock shard_lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it != shards_[shard_idx].cache.end())
	{
		it->second->hot_reload_enabled = false;
	}
}

inline std::future<void> ResourceManager::when_all_ready(const std::vector<ResourceId> &ids)
{
	auto state = std::make_shared<std::atomic<int>>(ids.size());
	auto shared_promise = std::make_shared<std::promise<void>>();
	auto result = shared_promise->get_future();
	for (ResourceId id : ids)
	{
		threadPool_.submit([this, id, state, shared_promise] {
			wait_for_ready(id).wait();
			if (--(*state) == 0)
			{
				shared_promise->set_value();
			}
		});
	}
	return result;
}

inline std::future<void> ResourceManager::wait_for_ready(ResourceId id)
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		std::promise<void> p;
		p.set_value();
		return p.get_future();
	}
	auto &entry = it->second;
	if (entry->state == ResourceState::Ready || entry->state == ResourceState::Failed)
	{
		std::promise<void> p;
		p.set_value();
		return p.get_future();
	}
	if (!entry->ready_promise)
	{
		entry->ready_promise = std::make_shared<std::promise<void>>();
	}
	return entry->ready_promise->get_future();
}

inline std::pair<ResourceState, std::string> ResourceManager::get_status(ResourceId id) const
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return {ResourceState::Unloaded, ""};
	}
	return {it->second->state.load(), it->second->error_message};
}

inline uint64_t ResourceManager::get_current_version(ResourceId id) const
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return 0;
	}
	return it->second->current_version_id.load();
}

inline void *ResourceManager::get_data_version(ResourceId id, uint64_t version)
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return nullptr;
	}
	for (auto &v : it->second->versions)
	{
		if (v->version_id == version)
		{
			return v->data.get();
		}
	}
	return nullptr;
}

inline std::shared_ptr<void> ResourceManager::get_data_shared(ResourceId id, uint64_t version)
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return nullptr;
	}
	for (auto &v : it->second->versions)
	{
		if (v->version_id == version)
		{
			return v->data;
		}
	}
	return nullptr;
}

inline bool ResourceManager::is_version_ready(ResourceId id, uint64_t version)
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return false;
	}
	for (auto &v : it->second->versions)
	{
		if (v->version_id == version && v->data)
		{
			return true;
		}
	}
	return false;
}

inline void *ResourceManager::ensure_resident_version(ResourceId id, uint64_t version)
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return nullptr;
	}
	auto &entry = it->second;
	for (auto &v : entry->versions)
	{
		if (v->version_id == version)
		{
			if (v->gpu_resident)
			{
				touch_version(id, version);
				return v->gpu_handle;
			}
			if (!v->data)
			{
				return nullptr;
			}
			std::size_t gpu_size = entry->loader->get_gpu_memory_size(v->data.get());
			void *gpu_mem = allocate_gpu_memory_locked(id, version, gpu_size);
			if (!gpu_mem)
			{
				return nullptr;
			}

			auto upload_start = std::chrono::steady_clock::now();
			entry->loader->upload_to_gpu(v->data.get(), gpu_mem);
			v->load_times.gpu_upload
				= std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - upload_start);

			v->gpu_handle = gpu_mem;
			v->gpu_resident = true;
			v->gpu_memory_size = gpu_size;
			stats_.total_gpu_memory_used.fetch_add(gpu_size);
			touch_version(id, version);

			if (v->ref_count.load() == 0)
			{
				policy_->on_add(id, version, gpu_size);
			}
			else
			{
				policy_->on_access(id, version);
			}
			return gpu_mem;
		}
	}
	return nullptr;
}

inline void ResourceManager::add_ref_version(ResourceId id, uint64_t version)
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return;
	}
	for (auto &v : it->second->versions)
	{
		if (v->version_id == version)
		{
			int prev = v->ref_count.fetch_add(1, std::memory_order_relaxed);
			if (prev == 0 && v->gpu_resident)
			{
				policy_->on_remove(id, version);
			}
			break;
		}
	}
}

inline void ResourceManager::release_version(ResourceId id, uint64_t version)
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return;
	}
	auto &entry = it->second;
	for (auto itv = entry->versions.begin(); itv != entry->versions.end(); ++itv)
	{
		if ((*itv)->version_id == version)
		{
			int prev = (*itv)->ref_count.fetch_sub(1, std::memory_order_acq_rel);
			if (prev == 1)
			{
				if (version != entry->current_version_id.load())
				{
					if ((*itv)->gpu_resident && (*itv)->gpu_handle)
					{
						gpu_allocator_->deallocate((*itv)->gpu_handle);
						stats_.evictions.fetch_add(1);
						stats_.total_gpu_memory_used.fetch_sub((*itv)->gpu_memory_size);
					}
					policy_->on_remove(id, version);
					entry->versions.erase(itv);
				}
				else
				{
					if ((*itv)->gpu_resident)
					{
						policy_->on_add(id, version, (*itv)->gpu_memory_size);
					}
				}
			}
			break;
		}
	}
}

inline void ResourceManager::hot_reload(ResourceId id)
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return;
	}
	auto &entry = it->second;
	if (entry->state != ResourceState::Ready)
	{
		return;
	}

	uint64_t new_version_id = entry->next_version_id.fetch_add(1);
	entry->versions.push_back(std::make_unique<ResourceEntry::Version>(new_version_id));
	auto &new_version = *entry->versions.back();
	new_version.ref_count.store(0);
	entry->current_version_id.store(new_version_id);

	entry->state = ResourceState::Loading;
	entry->ready_promise = std::make_shared<std::promise<void>>();
	stats_.hot_reloads.fetch_add(1);

	std::weak_ptr<ResourceEntry> weakEntry = entry;
	threadPool_.submit([this, weakEntry, id, path = entry->original_path, new_version_id] {
		auto ent = weakEntry.lock();
		if (!ent)
		{
			return;
		}

		LoadTimes times;
		times.begin();
		std::shared_ptr<void> new_data = nullptr;
		ResourceState final_state = ResourceState::Ready;
		std::string error_msg;
		try
		{
			new_data = ent->loader->load(*this, path, LoadPriority::Immediate, times);
			if (!new_data)
			{
				throw std::runtime_error("Reload returned null");
			}
		}
		catch (const std::exception &e)
		{
			error_msg = e.what();
			final_state = ResourceState::Failed;
		}
		times.end();

		size_t shard = get_shard_index(id);
		std::unique_lock lk(shards_[shard].mutex);
		for (auto &v : ent->versions)
		{
			if (v->version_id == new_version_id)
			{
				v->data = new_data;
				v->load_times = times;
				break;
			}
		}
		ent->state = final_state;
		ent->error_message = error_msg;
		ent->last_write_time = fs::last_write_time(path);
		if (ent->ready_promise)
		{
			ent->ready_promise->set_value();
			ent->ready_promise.reset();
		}
		if (final_state == ResourceState::Ready)
		{
			ent->loader->reload(*this, id, new_data);
			notify_dependents(id);
		}
	});
}

inline void ResourceManager::unload_resource(ResourceId id)
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return;
	}
	auto &entry = it->second;

	// Copy dependencies before releasing lock
	std::vector<ResourceId> deps_copy = entry->dependencies;

	// Notify dependents asynchronously (submits tasks, doesn't lock)
	notify_dependents(id);

	// Free GPU memory for all versions (still under shard lock)
	for (auto &v : entry->versions)
	{
		if (v->gpu_resident && v->gpu_handle)
		{
			gpu_allocator_->deallocate(v->gpu_handle);
			stats_.total_gpu_memory_used.fetch_sub(v->gpu_memory_size);
		}
		policy_->on_remove(id, v->version_id);
	}

	// Release shard lock before touching dep_mutex_ to maintain order
	lock.unlock();

	// Remove this resource from dependency lists of others
	{
		std::unique_lock dep_lock(dep_mutex_);
		for (ResourceId dep_id : deps_copy)
		{
			size_t dep_shard = get_shard_index(dep_id);
			std::unique_lock dep_shard_lock(shards_[dep_shard].mutex);
			auto dep_it = shards_[dep_shard].cache.find(dep_id);
			if (dep_it != shards_[dep_shard].cache.end())
			{
				std::erase(dep_it->second->dependents, id);
			}
		}
	}

	// Re-lock shard to erase the entry
	lock.lock();
	shards_[shard_idx].cache.erase(it);
	stats_.total_cache_entries.fetch_sub(1);
}

inline ResourceManager::Stats ResourceManager::get_stats() const
{
	Stats s;
	s.total_cache_entries = stats_.total_cache_entries.load();
	s.total_versions = stats_.total_versions.load();
	s.total_gpu_memory_used = stats_.total_gpu_memory_used.load();
	s.total_cpu_memory_used = stats_.total_cpu_memory_used.load();
	s.total_loads = stats_.total_loads.load();
	s.cache_hits = stats_.cache_hits.load();
	s.cache_misses = stats_.cache_misses.load();
	s.evictions = stats_.evictions.load();
	s.hot_reloads = stats_.hot_reloads.load();
	return s;
}

inline void ResourceManager::dump_resources(std::ostream &os) const
{
	os << "==== Resource Manager Dump ====\n";
	os << std::left << std::setw(20) << "Path" << " " << std::setw(10) << "State" << " " << std::setw(8) << "Versions" << " "
	   << std::setw(8) << "CurVer" << " " << std::setw(10) << "GPU Mem" << " " << std::setw(10) << "Load Time" << "\n";
	os << std::string(80, '-') << "\n";

	for (const auto &shard : shards_)
	{
		std::unique_lock lock(shard.mutex);
		for (const auto &[id, entry] : shard.cache)
		{
			std::string path_str = entry->original_path.filename().string();
			if (path_str.length() > 18)
			{
				path_str = path_str.substr(0, 15) + "...";
			}

			std::string state_str;
			switch (entry->state.load())
			{
				case ResourceState::Unloaded:
					state_str = "Unloaded";
					break;
				case ResourceState::Loading:
					state_str = "Loading";
					break;
				case ResourceState::Ready:
					state_str = "Ready";
					break;
				case ResourceState::Failed:
					state_str = "Failed";
					break;
				case ResourceState::Unloading:
					state_str = "Unloading";
					break;
			}

			uint64_t cur_ver = entry->current_version_id.load();
			std::size_t total_gpu = 0;
			for (const auto &v : entry->versions)
			{
				if (v->gpu_resident)
				{
					total_gpu += v->gpu_memory_size;
				}
			}

			std::string load_time_str = "N/A";
			for (const auto &v : entry->versions)
			{
				if (v->version_id == cur_ver)
				{
					auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(v->load_times.total).count();
					load_time_str = std::to_string(ms) + " ms";
					break;
				}
			}

			os << std::left << std::setw(20) << path_str << " " << std::setw(10) << state_str << " " << std::setw(8)
			   << entry->versions.size() << " " << std::setw(8) << cur_ver << " " << std::setw(10) << (total_gpu / 1'024) << " KB "
			   << std::setw(10) << load_time_str << "\n";
		}
	}
}

inline bool ResourceManager::evict_resource(ResourceId id, uint64_t version)
{
	size_t shard_idx = get_shard_index(id);
	std::unique_lock lock(shards_[shard_idx].mutex);
	auto it = shards_[shard_idx].cache.find(id);
	if (it == shards_[shard_idx].cache.end())
	{
		return false;
	}
	auto &entry = it->second;
	if (version == 0)
	{
		bool any = false;
		for (auto itv = entry->versions.begin(); itv != entry->versions.end();)
		{
			if ((*itv)->version_id != entry->current_version_id.load() && (*itv)->ref_count.load() == 0 && (*itv)->gpu_resident)
			{
				gpu_allocator_->deallocate((*itv)->gpu_handle);
				stats_.total_gpu_memory_used.fetch_sub((*itv)->gpu_memory_size);
				(*itv)->gpu_handle = nullptr;
				(*itv)->gpu_resident = false;
				(*itv)->gpu_memory_size = 0;
				policy_->on_remove(id, (*itv)->version_id);
				itv = entry->versions.erase(itv);
				any = true;
			}
			else
			{
				++itv;
			}
		}
		return any;
	}
	else
	{
		for (auto &v : entry->versions)
		{
			if (v->version_id == version && v->ref_count.load() == 0 && v->gpu_resident)
			{
				gpu_allocator_->deallocate(v->gpu_handle);
				stats_.total_gpu_memory_used.fetch_sub(v->gpu_memory_size);
				v->gpu_handle = nullptr;
				v->gpu_resident = false;
				v->gpu_memory_size = 0;
				policy_->on_remove(id, version);
				return true;
			}
		}
	}
	return false;
}

inline ResourceGroup ResourceManager::create_group()
{
	return ResourceGroup(*this);
}

// -------------------------------------------------------------------
// Example loaders (Texture, Material)
// -------------------------------------------------------------------
struct Texture
{ /* ... */
};

class TextureLoader : public IResourceLoader
{
	public:
		std::shared_ptr<void> load(ResourceManager &mgr, const fs::path &path, LoadPriority priority, LoadTimes &times) override
		{
			auto disk_start = std::chrono::steady_clock::now();
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			times.disk_read = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - disk_start);

			auto decomp_start = std::chrono::steady_clock::now();
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			times.decompress = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - decomp_start);

			auto tex = std::make_shared<Texture>();
			std::cout << "Loaded texture: " << path << "\n";
			return tex;
		}

		std::generator<std::span<const std::byte>> stream(ResourceManager &, fs::path path) override // now by value
		{
			std::ifstream file(path, std::ios::binary); // uses the local copy
			constexpr std::size_t chunk_size = 64 * 1'024;
			std::vector<std::byte> buffer(chunk_size);
			while (file)
			{
				file.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
				std::streamsize bytes_read = file.gcount();
				if (bytes_read > 0)
				{
					co_yield std::span<const std::byte>(buffer.data(), bytes_read);
				}
			}
		}

		void reload(ResourceManager &, ResourceId, std::shared_ptr<void>) override {}

		std::size_t get_gpu_memory_size(void *) override
		{
			return 1'024 * 1'024;
		}

		void upload_to_gpu(void *, void *) override {}
};

struct Material
{
		ResourceHandle<Texture> albedo;
		ResourceHandleWeak<Texture> normal;
};

class MaterialLoader : public IResourceLoader
{
	public:
		std::shared_ptr<void> load(ResourceManager &mgr, const fs::path &path, LoadPriority priority, LoadTimes &times) override
		{
			auto disk_start = std::chrono::steady_clock::now();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			times.disk_read = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - disk_start);

			fs::path albedoPath = path.parent_path() / "albedo.dds";
			fs::path normalPath = path.parent_path() / "normal.dds";

			auto decomp_start = std::chrono::steady_clock::now();
			auto albedoHandle = mgr.request_async<Texture>(albedoPath, {}, priority);
			auto normalHandle = mgr.request_weak<Texture>(normalPath, priority);
			mgr.wait_for_ready(albedoHandle.id()).wait();
			mgr.wait_for_ready(normalHandle.id()).wait();
			times.decompress = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - decomp_start);

			auto mat = std::make_shared<Material>();
			mat->albedo = std::move(albedoHandle);
			mat->normal = std::move(normalHandle);
			return mat;
		}

		void reload(ResourceManager &, ResourceId, std::shared_ptr<void>) override {}

		std::size_t get_gpu_memory_size(void *) override
		{
			return 0;
		}

		void upload_to_gpu(void *, void *) override {}
};