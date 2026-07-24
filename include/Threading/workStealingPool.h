/*! @file workStealingPool.h
	@brief Public declaration of the WorkStealingPool thread pool and task submission helpers.
	@details Provides a simple work-stealing thread pool used to submit arbitrary tasks and
			 chunked work for parallel processing across a set of worker threads. Tasks are
			 distributed to per-worker `WorkStealingQueue` instances and may be stolen by
			 idle workers to balance load.
	@date 02/20/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_WORKSTEALINGPOOL_H
#define INCLUDE_ECS_WORKSTEALINGPOOL_H

#include <atomic>
#include <future>
#include <latch>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "Core/cconcepts.h"
#include "Core/typedefs.h"
#include "ECS/archetype.h"

#include "workStealingQueue.h"

namespace Dimensia::Threading
{
	using Dimensia::Core::ui;
	using Dimensia::ECS::Archetype;

	using ChunkVector = std::vector<std::pair<Archetype *, ui>>;

	/*! @class WorkStealingPool include/Threading/workStealingPool.h
		@brief A fixed-size thread pool with per-worker work-stealing queues.
		@details The pool creates `numThreads` workers on construction. Tasks submitted
				 via `submitTask` are pushed onto a per-worker `WorkStealingQueue` using
				 a simple round-robin index. Workers pop local tasks and will attempt to
				 steal from other workers when idle. The destructor signals workers to
				 stop and joins them; it pushes a `nullptr` sentinel task to each queue
				 to ensure worker termination.
		@note Thread-safe for concurrent task submissions. Internal synchronization
			  is handled by the per-worker `WorkStealingQueue` implementation.
		@warning Copy and move operations are deleted; the pool manages thread lifetimes
				 and must not be copied or moved.
		@date 02/20/2026
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	class WorkStealingPool
	{
		public:
			// MARK: Constructor, Assignment Operators, and Destructor

			/*! @brief Construct a WorkStealingPool with the given number of workers.
				@param[in] numThreads Number of worker threads to create. Default is the
									  value returned by `std::thread::hardware_concurrency()`.
				@pre `numThreads > 0` is recommended; behavior with `0` depends on
					 platform `hardware_concurrency()` and callers should avoid `0`.
				@post Worker threads are created and begin executing `worker_loop`.
			*/
			explicit WorkStealingPool(const std::size_t numThreads = std::thread::hardware_concurrency());

			WorkStealingPool(const WorkStealingPool &other) = delete;
			WorkStealingPool(WorkStealingPool &&other) noexcept = delete;
			WorkStealingPool &operator=(const WorkStealingPool &other) = delete;
			WorkStealingPool &operator=(WorkStealingPool &&other) noexcept = delete;

			/*! @brief Stop the pool and join all worker threads.
				@details Sets an internal stop flag, pushes a `nullptr` sentinel task into
						 each worker queue to unblock waiting workers, and joins all
						 worker threads. After destruction all resources are released.
				@note Destructor blocks until all workers have exited.
			*/
			~WorkStealingPool();

			// MARK: Template Function

			/*! @brief Submit a collection of chunks to be processed in parallel.
				@tparam Func A callable invocable as `func(@ref Dimensia::ECS::Archetype* "Archetype*", ui)`; must satisfy
			   @ref Dimensia::Core::InvocableWithArgs<Func, Dimensia::ECS::Archetype*, ui> "InvocableWithArgs<Func, Archetype*, ui>".
				@param[in] chunks A vector of (@ref Dimensia::ECS::Archetype* "Archetype*", index) pairs representing work.
				@param[in] func A callable invoked for each chunk with `(@ref Dimensia::ECS::Archetype* "Archetype*", ui)`.
				@param[in,out] latch A `std::latch` that is decremented once per submitted
									 task (each batch) after the work for that batch completes.
				@param[in] batchSize Number of chunks grouped into a single task; must be > 0.
				@details The function groups `chunks` into batches of `batchSize` and
						 submits each batch as a task to the pool. Each submitted task
						 calls `func` for every element in its batch and then calls
						 `latch.count_down()` to signal completion.
				@pre `batchSize > 0`. The caller is responsible for initializing `latch`
					 to the number of tasks that will be submitted.
				@return One future per submitted batch. Calling `get()` propagates exceptions raised while processing that batch.
				@note Work is distributed using `submitTask` which pushes tasks to per-worker
					  queues in a round-robin fashion. Complexity is O(N) in the number
					  of input `chunks`.
			*/
			template <typename Func>
				requires Dimensia::Core::InvocableWithArgs<Func, Archetype *, ui>
			std::vector<std::future<void>> submitChunks(const ChunkVector &chunks, Func &&func, std::latch &latch,
														const std::size_t batchSize)
			{
				ChunkVector batch;
				batch.reserve(batchSize);
				std::vector<std::future<void>> futures;
				futures.reserve((chunks.size() + batchSize - 1) / batchSize);

				const Func forwardedFunc{std::forward<Func>(func)};
				auto submitBatch = [&](const ChunkVector &batchToSubmit) {
					auto packagedTask{std::make_shared<std::packaged_task<void()>>([batchToSubmit, forwardedFunc] {
						for (const auto &[arch, index] : batchToSubmit)
						{
							forwardedFunc(arch, index);
						}
					})};
					futures.push_back(packagedTask->get_future());
					submitTask([packagedTask, &latch] {
						(*packagedTask)();
						latch.count_down();
					});
				};

				for (const auto &chunk : chunks)
				{
					batch.push_back(chunk);

					if (batch.size() >= batchSize)
					{
						submitBatch(batch);
						batch.clear();
					}
				}

				if (!batch.empty())
				{
					submitBatch(batch);
				}

				return futures;
			}

		private:
			// MARK: Private Template Member Functions

			/*! @brief Submit a no-argument callable to the pool.
				@tparam Func A callable invocable with no arguments; must satisfy @ref Dimensia::Core::InvocableNoArgs
			   "InvocableNoArgs<Func>".
				@param[in] task The callable to execute by a worker thread.
				@details Tasks are assigned to a worker queue using a simple round-robin
						 index computed from the atomic `mTaskCount`. The callable is
						 forwarded into the chosen `WorkStealingQueue`.
				@pre The pool must remain alive while the task is pending or executing.
				@note This function may be called concurrently from multiple threads.
			*/
			template <typename Func>
				requires Dimensia::Core::InvocableNoArgs<Func>
			void submitTask(Func &&task)
			{
				const std::size_t index{mTaskCount++ % mQueues.size()};

				assert(index < mQueues.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				mQueues[index].push(std::forward<Func>(task));
			}

			// MARK: Private Member Functions

			/*! @brief Main loop run by each worker thread.
				@param[in] workerID Index of the worker and its associated queue in `mQueues`.
				@details The worker attempts to pop a local task. If none is available it
						 will attempt to steal from other workers. A `nullptr` task
						 serves as a sentinel indicating the worker should exit.
			*/
			void workerLoop(const std::size_t workerID);

		private:
			/*! @var mWorkers
				@brief Worker threads owned by the pool.
				@details Each element runs `workerLoop` and is joined in the destructor.
			*/
			std::vector<std::thread> mWorkers{};

			/*! @var mQueues
				@brief Per-worker `WorkStealingQueue` instances.
				@details Each worker primarily pops from its corresponding queue; idle
						 workers attempt to steal tasks from other queues to balance load.
			*/
			std::vector<WorkStealingQueue> mQueues;

			/*! @var mTaskCount
				@brief Atomic counter used for round-robin task assignment.
				@details Incremented for each submitted task; index is computed as
						 `mTaskCount++ % mQueues.size()` when pushing tasks.
			*/
			std::atomic<std::size_t> mTaskCount;

			/*! @var mStop
				@brief Atomic flag that indicates workers should stop.
				@details Set to true during destruction to cause `worker_loop` to exit.
			*/
			std::atomic<bool> mStop;
	};
} // namespace Dimensia::Threading

#endif