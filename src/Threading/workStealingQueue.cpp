/*! \file workStealingQueue.cpp
	\brief Contains the function definitions for creating a workStealingQueue
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "Threading/workStealingQueue.h"

#include <cstddef>
#include <mutex>
#include <utility>

namespace Dimensia::Threading
{
	// MARK: Member Functions

	bool WorkStealingQueue::tryPop(Task &task)
	{
		const std::scoped_lock<std::mutex> lock(mMutex);

		if (mQueue.empty())
		{
			return false;
		}

		task = std::move(mQueue.front());
		mQueue.pop_front();

		return true;
	}

	bool WorkStealingQueue::trySteal(Task &task)
	{
		const std::scoped_lock<std::mutex> lock(mMutex);

		if (mQueue.empty())
		{
			return false;
		}

		task = std::move(mQueue.back());
		mQueue.pop_back();

		return true;
	}

	std::size_t WorkStealingQueue::size() const
	{
		const std::scoped_lock<std::mutex> lock(mMutex);

		return mQueue.size();
	}
} // namespace Dimensia::Threading