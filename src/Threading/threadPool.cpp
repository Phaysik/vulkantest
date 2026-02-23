/*! \file threadPool.cpp
	\brief Contains the function definitions for creating a threadPool
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "Threading/threadPool.h"

#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace Dimensia::Threading
{
	// MARK: Constructor and Destructor

	ThreadPool::ThreadPool(const std::size_t numThreads) : mStop(false)
	{
		for (std::size_t i{0}; i < numThreads; ++i)
		{
			mWorkers.emplace_back([this] {
				while (true)
				{
					std::function<void()> task;

					{
						std::unique_lock<std::mutex> lock(mQueueMutex);
						mCondition.wait(lock, [this] { return mStop || !mTasks.empty(); });

						if (mStop && mTasks.empty())
						{
							return;
						}

						task = std::move(mTasks.front());
						mTasks.pop();
					}

					task();
				}
			});
		}
	}

	ThreadPool::~ThreadPool()
	{
		{
			const std::scoped_lock<std::mutex> lock(mQueueMutex);
			mStop = true;
		}

		mCondition.notify_all();

		for (std::thread &worker : mWorkers)
		{
			worker.join();
		}
	}
} // namespace Dimensia::Threading