/*! \file workStealingQueue.cpp
	\brief Contains the function definitions for creating a workStealingQueue
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "Threading/workStealingQueue.h"

namespace Dimensia::Threading
{
	void WorkStealingQueue::push(Task task)
	{
		std::unique_lock<std::mutex> lock(mutex);
		queue.push_back(std::move(task));
	}

	bool WorkStealingQueue::try_pop(Task &task)
	{
		std::unique_lock<std::mutex> lock(mutex);
		if (queue.empty())
		{
			return false;
		}
		task = std::move(queue.front());
		queue.pop_front();
		return true;
	}

	bool WorkStealingQueue::try_steal(Task &task)
	{
		std::unique_lock<std::mutex> lock(mutex);
		if (queue.empty())
		{
			return false;
		}
		task = std::move(queue.back());
		queue.pop_back();
		return true;
	}

	size_t WorkStealingQueue::size() const
	{
		std::unique_lock<std::mutex> lock(mutex);
		return queue.size();
	}
} // namespace Dimensia::Threading