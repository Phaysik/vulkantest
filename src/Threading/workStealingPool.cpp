/*! \file workStealingPool.cpp
	\brief Contains the function definitions for creating a workStealingPool
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "Threading/workStealingPool.h"

namespace Dimensia::Threading
{
	WorkStealingPool::WorkStealingPool(size_t numThreads) : stop(false), taskCount(0), queues(numThreads)
	{
		for (size_t i = 0; i < numThreads; ++i)
		{
			workers.emplace_back([this, i] { worker_loop(i); });
		}
	}

	WorkStealingPool::~WorkStealingPool()
	{
		stop = true;
		for (auto &q : queues)
		{
			q.push(nullptr); // Sentinel
		}
		for (auto &w : workers)
		{
			w.join();
		}
	}

	void WorkStealingPool::submit_task(std::function<void()> task)
	{
		size_t idx = taskCount++ % queues.size();
		queues[idx].push(std::move(task));
	}

	void WorkStealingPool::worker_loop(size_t workerId)
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
			for (size_t i = 1; i < queues.size(); ++i)
			{
				size_t victimId = (workerId + i) % queues.size();
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
} // namespace Dimensia::Threading