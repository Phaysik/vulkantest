/*! \file threadPool.cpp
	\brief Contains the function definitions for creating a threadPool
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "Threading/threadPool.h"

namespace Dimensia::Threading
{
	ThreadPool::ThreadPool(size_t numThreads) : stop(false)
	{
		for (size_t i = 0; i < numThreads; ++i)
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

	ThreadPool::~ThreadPool()
	{
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			stop = true;
		}
		condition.notify_all();
		for (auto &w : workers)
		{
			w.join();
		}
	}

	void ThreadPool::submit_with_latch(std::function<void()> task, Latch &latch) const
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
} // namespace Dimensia::Threading