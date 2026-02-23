/*! @file workStealingQueue.h
	@brief Lightweight, thread-safe work-stealing queue for Task objects used by a thread pool or worker threads.
	@details The queue provides push, pop, and steal operations guarded by an internal mutex. It is intended
	for use with worker threads where one thread pushes and pops from the front while other threads may
	attempt to steal work from the back. All operations are protected by the mutex to ensure safe concurrent
	access.
	@date 02/23/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_WORKSTEALINGQUEUE_H
#define INCLUDE_ECS_WORKSTEALINGQUEUE_H

#include <deque>
#include <functional>
#include <mutex>

#include "Core/cconcepts.h"

namespace Dimensia::Threading
{
	/*! @class WorkStealingQueue include/Threading/workStealingQueue.h
		@brief Queue that supports pushing tasks, popping by owner, and stealing by other threads.
		@details Each queued element is a `Task` (alias for `std::function<void()>`). The internal
		container is a `std::deque` which allows efficient push/pop operations at both ends. All public
		member functions synchronize access using an internal `std::mutex`.
		@note Thread-safety: Thread-safe for concurrent access — all public operations acquire the internal
		mutex. Callers should still ensure higher-level coordination (for example, worker lifecycle) as
		appropriate for their executor design.
		@date 02/23/2026
	*/
	class WorkStealingQueue
	{
		public:
			/*! @typedef Task
				@brief Alias for the callable task type stored in the queue.
				@details Tasks must be callable with no arguments and no return value.
			*/
			using Task = std::function<void()>;

			// MARK: Member Functions

			/*! @brief Attempts to pop a task from the front of the queue.
				@param[out] task Receives the popped task if the call returns `true`.
				@return `true` and moves a task into `task` if the queue was non-empty; `false` otherwise.
				@post When `true` is returned, the caller owns the moved task and may invoke it.
				@note Complexity: O(1).
			*/
			bool tryPop(Task &task);

			/*! @brief Attempts to steal a task from the back of the queue.
				@param[out] task Receives the stolen task if the call returns `true`.
				@return `true` and moves a task into `task` if the queue was non-empty; `false` otherwise.
				@details Stealing is intended for other worker threads to balance load. Semantically this
				removes an element from the back while `tryPop` removes from the front.
				@note Complexity: O(1).
			*/
			bool trySteal(Task &task);

			/*! @brief Returns the current number of tasks in the queue.
				@return The number of tasks currently stored.
				@note This call acquires the internal mutex and therefore reflects a synchronized snapshot.
				@note Complexity: O(1).
			*/
			std::size_t size() const;

			// MARK: Template Member Functions

			/*! @brief Enqueues a callable object into the work-stealing queue.
				@tparam Func A callable type invocable with no arguments (satisfies @ref Dimensia::Core::InvocableNoArgs "InvocableNoArgs").
				@param[in] task Rvalue reference to the callable to be stored. The callable is moved into the
				internal queue; the passed `task` will be in a moved-from state after the call.
				@pre `Func` must be callable with no arguments and no return value.
				@post The callable is appended to the back of the queue and will be available for
				`tryPop`/`trySteal` by worker threads.
				@note Complexity: Amortized O(1). This method acquires the internal mutex to synchronize access.
			*/
			template <typename Func>
				requires Dimensia::Core::InvocableNoArgs<Func>
			void push(Func &&task)
			{
				const std::scoped_lock<std::mutex> lock(mMutex);

				mQueue.push_back(std::forward<Func>(task));
			}

		private:
			/*! @var mQueue
				@brief Container holding pending tasks.
				@details `std::deque` is chosen to allow efficient push/pop at both ends required by
				pop/steal semantics. The queue owns the stored `Task` objects.
				@showinitializer
			*/
			std::deque<Task> mQueue;

			/*! @var mMutex
				@brief Mutex protecting the `mQueue` for all public operations.
				@details Marked `mutable` to allow `size()` to be `const` while still synchronizing access.
				@note Ownership: the class manages this mutex internally; callers must not attempt to lock it.
			*/
			mutable std::mutex mMutex;
	};
} // namespace Dimensia::Threading

#endif