/*! \file workStealingPool.cpp
	\brief Contains the function definitions for creating a workStealingPool
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "Threading/workStealingPool.h"

#include <cassert>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

namespace Dimensia::Threading
{
	// MARK: Constructor and Destructor

	WorkStealingPool::WorkStealingPool(const std::size_t numThreads) : mQueues(numThreads), mTaskCount(0), mStop(false)
	{
		for (std::size_t i{0}; i < numThreads; ++i)
		{
			mWorkers.emplace_back([this, i] { workerLoop(i); });
		}
	}

	WorkStealingPool::~WorkStealingPool()
	{
		mStop = true;

		for (std::thread &worker : mWorkers)
		{
			worker.join();
		}
	}

	// MARK: Private Member Functions

	void WorkStealingPool::workerLoop(const std::size_t workerID)
	{
		while (!mStop)
		{
			std::function<void()> task;

			assert(workerID < mQueues.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			if (mQueues[workerID].tryPop(task))
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
				if (mQueues[victimId].trySteal(task))
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