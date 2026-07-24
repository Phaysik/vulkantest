/*! @file commandBuffer.h
	@brief Thread-local command buffering and application helpers.
	@details `CommandBuffer` provides a threadsafe way to enqueue ECS mutation commands from multiple producer threads. Commands are stored
   per-thread in `ThreadBuffer` instances and merged/consumed by calling `apply(ECS &)` on a single consumer. The implementation partitions
   command lists to process adds first, then removes, destroys, and finally parent assignments to ensure predictable ordering and to
   minimize temporary state during application.
	@note Thread-safe for concurrent recording, playback, and clearing. Playback and clearing are serialized. A command racing with playback
	may be consumed by the current snapshot or the next snapshot; a command racing with clearing may be cleared or remain queued.
	@date 02/14/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMMANDBUFFER_H
#define INCLUDE_ECS_COMMANDBUFFER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "command.h"
#include "componentRegistry.h"
#include "entity.h"

namespace Dimensia::ECS
{
	// Forward declaration
	class ECS;

	using Dimensia::Registry::componentID;
	using Dimensia::Registry::ComponentTypeID;

	/*! @struct ThreadBuffer include/ECS/commandBuffer.h
		@brief Per-thread storage for enqueued `Command` values.
		@details Each producer thread obtains a `ThreadBuffer` via `getThreadBuffer()` and appends `Command` objects to `commands`. The
	   `CommandBuffer::apply()` implementation moves these vectors out under a lock and then processes them without holding the lock to
	   reduce contention.
	*/
	struct ThreadBuffer
	{
		public:
			ThreadBuffer() : mutex(), commands() {}

		private:
			std::mutex mutex;
			std::vector<Command> commands;

			friend class CommandBuffer;
	};

	/*! @class CommandBuffer include/ECS/commandBuffer.h
		@brief Thread-local command queue and application helper.
		@details Producers enqueue commands into a per-thread `ThreadBuffer` obtained from `getThreadBuffer()`. `apply(ECS &)` merges
	   per-thread command lists under a lock, then processes each list outside the lock to apply changes to the ECS. The buffer supports
	   adding components, removing components, destroying entities, and setting parent relationships via simple, exception-safe APIs.
	*/
	class CommandBuffer
	{
		public:
			/*! @brief Constructs an empty command buffer with synchronized producer and consumer state. */
			CommandBuffer() : mBuffers(), mMapMutex(), mConsumerMutex() {}

			// MARK: Public Member Functions

			/*! @brief Enqueue a `SetParent` command for later application.
				@param[in] child Child entity whose parent will be set.
				@param[in] parent Parent entity to assign.
			*/
			void setParent(const Entity &child, const Entity &parent);

			/*! @brief Enqueue a `Destroy` command for later application.
				@param[in] entity Entity to destroy when `apply()` is called.
			*/
			void destroy(const Entity &entity);

			/*! @brief Apply all queued commands to the provided `ECS` instance.
				@details Detaches all per-thread command lists, merges them globally, and sorts them by phase and monotonic recording
			   sequence. Phase order is: 1) Adds, 2) Removes, 3) Destroys, 4) SetParent. Within a phase, commands execute in recording
			   order. Therefore a later add replaces an earlier add, remove wins over add in the same playback, destroy wins over component
			   operations, and parent assignments are attempted last.
				@param[in,out] ecs The `ECS` instance to mutate.
			*/
			void apply(ECS &ecs);

			/*! @brief Clears commands currently present in all registered per-thread buffers.
				@post Registered thread buffers remain allocated for reuse and contain no commands observed by this clear snapshot.
				@note A command recorded concurrently may be cleared or may remain queued for later playback.
			*/
			void clear();

			// MARK: Template Member Functions

			/*! @brief Enqueue an `AddComponent` command by copying/moving a value.
				@tparam T Component value type.
				@param[in] entity Target entity.
				@param[in] value Component value to add; forwarded into storage.
				@note Uses `Command::makeAdd` to construct the command.
			*/
			template <typename T>
			void addComponent(const Entity &entity, T &value)
			{
				T copy{value};
				record(Command::makeAdd(entity, std::move(copy)));
			}

			/*! @brief Enqueue an `AddComponent` command by forwarding a value.
				@tparam T Component value type.
				@param[in] entity Target entity.
				@param[in] value Component value to add; forwarded into storage.
			*/
			template <typename T>
			void addComponent(const Entity &entity, T &&value)
			{
				record(Command::makeAdd(entity, std::forward<T>(value)));
			}

			/*! @brief Enqueue a `RemoveComponent` command for component type `T`.
				@tparam T Component type to remove.
				@param[in] entity Target entity.
			*/
			template <typename T>
			void removeComponent(const Entity &entity)
			{
				record(Command::makeRemove(entity, componentID<T>()));
			}

		private:
			// MARK: Private Member Functions

			/*! @brief Return or create the `ThreadBuffer` for the calling thread.
				@return Pointer to the caller's `ThreadBuffer`.
			*/
			ThreadBuffer *getThreadBuffer();

			/*! @brief Appends a command to the calling thread's buffer.
				@param[in] command Command value to append.
				@note Thread-safe with concurrent producers, @ref apply, and @ref clear. A command racing with playback is consumed either
			   by that playback snapshot or the next one.
			*/
			void record(Command command);

			/*! @brief Helper used by `apply()` to perform an add operation on the ECS.
				@param[in,out] ecs The ECS instance to mutate.
				@param[in] entity Target entity for the add.
				@param[in] addData Payload describing the component to add.
			*/
			static void processAdd(ECS &ecs, const Entity &entity, const AddData &addData);

			/*! @brief Dispatch a remove operation to the ECS.
				@param[in,out] ecs The ECS instance to mutate.
				@param[in] entity Target entity.
				@param[in] componentTypeID Component type identifier to remove.
			*/
			static void dispatchRemove(ECS &ecs, const Entity &entity, const ComponentTypeID componentTypeID);

		private:
			/*! @var mBuffers
				@brief Map of thread IDs to per-thread `ThreadBuffer` instances.
				@details Stored buffers are heap allocated and reused across calls; the map is protected by `mMapMutex`.
			*/
			std::unordered_map<std::thread::id, std::unique_ptr<ThreadBuffer>> mBuffers;

			/*! @var mMapMutex
				@brief Mutex protecting `mBuffers` for registration of new threads and `apply()`/`clear()` operations.
			*/
			std::shared_mutex mMapMutex;

			/*! @var mConsumerMutex
				@brief Serializes command playback and clearing operations.
			*/
			std::mutex mConsumerMutex;

			/*! @var mNextSequence
				@brief Monotonic sequence assigned at each command recording linearization point.
			*/
			std::atomic<uint64_t> mNextSequence{0};

			/*! @var sNextInstanceID
				@brief Monotonically increasing counter used to assign unique IDs to `CommandBuffer` instances.
				@details Prevents ABA problems in the thread-local cache when a `CommandBuffer` is destroyed
			   and a new one is allocated at the same address.
			*/
			static inline std::atomic<uint32_t> sNextInstanceID{1};

			/*! @var mInstanceID
				@brief Unique identifier for this `CommandBuffer` instance, used by the thread-local cache in `getThreadBuffer()`.
			*/
			uint32_t mInstanceID{sNextInstanceID.fetch_add(1, std::memory_order_relaxed)};
	};
} // namespace Dimensia::ECS

#endif