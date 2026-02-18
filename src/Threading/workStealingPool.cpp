/*! \file workStealingPool.cpp
	\brief Contains the function definitions for creating a workStealingPool
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "Threading/workStealingPool.h"

#include "Threading/workStealingQueue.h"

namespace Dimensia::Threading
{
	// MARK: Constructor and Destructor

	WorkStealingPool::WorkStealingPool(const std::size_t numThreads) : mQueues(numThreads), mTaskCount(0), mStop(false)
	{
		for (std::size_t i{0}; i < numThreads; ++i)
		{
			mWorkers.emplace_back([this, i] { worker_loop(i); });
		}
	}

	WorkStealingPool::~WorkStealingPool()
	{
		mStop = true;

		for (WorkStealingQueue &queue : mQueues)
		{
			queue.push(nullptr); // Sentinel
		}
		for (std::thread &worker : mWorkers)
		{
			worker.join();
		}
	}

	// MARK: Private Member Functions

	void WorkStealingPool::submit_task(std::function<void()> &&task)
	{
		const std::size_t idx{mTaskCount++ % mQueues.size()};

		assert(idx < mQueues.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		mQueues[idx].push(std::move(task));
	}

	void WorkStealingPool::worker_loop(const std::size_t workerID)
	{
		while (!mStop)
		{
			std::function<void()> task;
			assert(workerID < mQueues.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			if (mQueues[workerID].try_pop(task))
			{
				if (!task)
				{
					break;
				}

				task();
				continue;
			}

			for (std::size_t i{1}; i < mQueues.size(); ++i)
			{
				const std::size_t victimId{(workerID + i) % mQueues.size()};

				assert(victimId < mQueues.size());

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				if (mQueues[victimId].try_steal(task))
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
} // namespace Dimensia::Threading