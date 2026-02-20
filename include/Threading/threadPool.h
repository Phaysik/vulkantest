/*! @file Threading/threadPool.h
	@brief ThreadPool implementation interface used to schedule tasks onto a worker pool.
	@details Provides a small thread pool abstraction used by the ECS to execute work
	items asynchronously. Tasks are stored as `std::function<void()>` and worker threads
	consume tasks from a synchronized queue.
	@date 02/14/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
 */

#ifndef INCLUDE_ECS_THREADPOOL_H
#define INCLUDE_ECS_THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <latch>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "Core/cconcepts.h"

namespace Dimensia::Threading
{
	using Dimensia::Core::InvocableNoArgs;

	/*! @class ThreadPool include/Threading/threadPool.h
		@brief Fixed-size thread pool for scheduling callable tasks onto worker threads.
		@details Manages a set of worker threads that consume tasks stored as
		`std::function<void()>` from a synchronized queue. Tasks may be submitted as
		callables with no arguments, callables with arguments (the arguments are
		decayed and stored), or submitted together with a `std::latch` which will be
		counted down after the task completes. Result-bearing submissions return a
		`std::future` carrying the result or any exception thrown by the task.

		@note Thread-safe for concurrent submissions: multiple threads may call the
		submit APIs concurrently. The caller must ensure that the `ThreadPool`
		object outlives any outstanding tasks and that the destructor is not called
		concurrently with submissions.

		@pre Prefer `numThreads > 0` for parallel execution. If `numThreads == 0`,
		no worker threads are created and queued tasks will not be executed until
		workers become available.

		@date 02/20/2026
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	class ThreadPool
	{
		public:
			// MARK: Constructors, Destructor, and Assignment Operators

			/*! @brief Construct a ThreadPool and start worker threads.
				@param[in] numThreads Number of worker threads to create. Defaults to
				`std::thread::hardware_concurrency()`; if zero, no workers are created.
				@post Worker threads are started and ready to consume tasks from the
				internal queue.
				@note If `numThreads == 0` the pool will accept tasks but they will
				not be executed until worker threads are available. Prefer a positive
				value for meaningful parallelism.
			*/
			explicit ThreadPool(const std::size_t numThreads = std::thread::hardware_concurrency());

			ThreadPool(const ThreadPool &other) = delete;
			ThreadPool(ThreadPool &&other) noexcept = delete;
			ThreadPool &operator=(const ThreadPool &other) = delete;
			ThreadPool &operator=(ThreadPool &&other) noexcept = delete;

			/*! @brief Join and stop all worker threads.
				@details Signals worker threads to stop, notifies the condition
				variable, and joins all threads. This call blocks until all worker
				threads have exited and all resources have been reclaimed.
				@note Do not call the destructor concurrently with submissions; the
				caller is responsible for synchronizing lifetime of the pool and
				outstanding tasks.
			*/
			~ThreadPool();

			// MARK: Template Member Function

			/*! @brief Submit a no-argument callable and count down a latch after completion.
				@tparam Func @ref Dimensia::Core::InvocableNoArgs "InvocableNoArgs": Callable type invocable with no arguments.
				@param[in] task The callable to execute on a worker thread.
				@param[in,out] latch The `std::latch` that will be decremented once the
				task has completed. The latch must outlive the task.
				@pre `task` must be invocable with no arguments. Use `std::ref` to
				pass references via the callable if required.
				@post `latch.count_down()` is called exactly once after `task()`
				completes (even if `task()` throws; exception propagation depends on
				the callable and is not captured by this API).
				@note Thread-safe for concurrent calls from multiple threads.
			*/
			template <typename Func>
				requires InvocableNoArgs<Func>
			void submit_with_latch(Func &&task, std::latch &latch) const
			{
				{
					const std::unique_lock<std::mutex> lock(mQueueMutex);

					mTasks.emplace([task = std::forward<Func>(task), &latch]() mutable {
						task();
						latch.count_down();
					});
				}

				mCondition.notify_one();
			}

			/*! @brief Submit a no-argument callable and obtain a future for its result.
				@tparam Func @ref Dimensia::Core::InvocableNoArgs "InvocableNoArgs": Callable type invocable with no arguments.
				@param[in] func The callable to execute on a worker thread.
				@return `std::future<return_type>` that becomes ready when the callable
				completes. If the callable throws, the exception is stored in the
				future and rethrown when the future is retrieved.
				@note The callable is wrapped in a `std::packaged_task` and executed
				on a worker thread. Use this overload for result-bearing operations.
				@note Thread-safe for concurrent calls from multiple threads.
			*/
			template <typename Func>
				requires InvocableNoArgs<Func>
			std::future<std::invoke_result_t<Func>> submit(Func &&func) const
			{
				using return_type = std::invoke_result_t<Func>;
				auto task{std::make_shared<std::packaged_task<return_type()>>(std::forward<Func>(func))};

				std::future<return_type> result = task->get_future();
				{
					const std::unique_lock<std::mutex> lock(mQueueMutex);
					mTasks.emplace([task]() mutable { (*task)(); });
				}

				mCondition.notify_one();
				return result;
			}

			/*! @brief Submit a callable with arguments and obtain a future for its result.
				@tparam Func @ref Dimensia::Core::InvocableWithArgs "InvocableWithArgs": Callable type invocable with `Args...`.
				@tparam Args Parameter pack of argument types. Arguments are decayed
				and stored by value in an internal `std::tuple`. Use `std::ref`
				explicitly to pass references.
				@param[in] func The callable to invoke on a worker thread.
				@param[in] args Arguments forwarded to the callable; they are stored
				by value (decayed) inside the task.
				@return `std::future<return_type>` that becomes ready when the
				callable completes. Exceptions thrown by the callable are stored in
				the returned future.
				@note The implementation captures the callable and arguments in a
				`std::shared_ptr<std::packaged_task<return_type()>>` and schedules it
				for execution. The internal lambda used for invocation is marked
				`noexcept` in the header to express intent; exceptions are still
				captured by the packaged task and propagated via the future.
				@note Thread-safe for concurrent calls from multiple threads.
			*/
			template <typename Func, typename... Args>
				requires Dimensia::Core::InvocableWithArgs<Func, Args...>
			std::future<std::invoke_result_t<Func, Args...>> submit(Func &&func, Args &&...args) const
			{
				using return_type = std::invoke_result_t<Func, Args...>;

				// Capture arguments in a tuple (decays to values for safety; use std::ref for references)
				auto args_tuple = std::make_tuple(std::forward<Args>(args)...);

				// Create a packaged_task that will invoke the function with the stored arguments
				auto task = std::make_shared<std::packaged_task<return_type()>>(
					[function = std::forward<Func>(func), args = std::move(args_tuple)]() mutable noexcept -> return_type {
						return std::apply(std::move(function), std::move(args));
					});

				std::future<return_type> result = task->get_future();
				{
					const std::unique_lock<std::mutex> lock(mQueueMutex);
					mTasks.emplace([task]() mutable { (*task)(); });
				}

				mCondition.notify_one();
				return result;
			}

		private:
			/*! @var mTasks
				@brief FIFO queue holding pending tasks as `std::function<void()>`.
				@note Access is synchronized by `mQueueMutex`.
			*/
			mutable std::queue<std::function<void()>> mTasks;

			/*! @var mQueueMutex
				@brief Mutex protecting access to `mTasks` and related state.
			*/
			mutable std::mutex mQueueMutex;

			/*! @var mCondition
				@brief Condition variable used to notify worker threads of new tasks
				or shutdown requests.
			*/
			mutable std::condition_variable mCondition;

			/*! @var mWorkers
				@brief Container owning the worker `std::thread` instances.
			*/
			std::vector<std::thread> mWorkers;

			/*! @var mStop
				@brief Atomic flag indicating the pool is stopping. When set to true,
				workers will exit once the task queue is empty.
			*/
			std::atomic<bool> mStop;
	};
} // namespace Dimensia::Threading
#endif