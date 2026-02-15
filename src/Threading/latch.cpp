/*! \file latch.cpp
	\brief Contains the function definitions for creating a latch
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "Threading/latch.h"

Latch::Latch(int count) : counter(count) {}

void Latch::count_down()
{
	if (counter.fetch_sub(1, std::memory_order_acq_rel) == 1)
	{
		cv.notify_all();
	}
}

void Latch::wait()
{
	std::unique_lock<std::mutex> lock(mutex);
	cv.wait(lock, [this] { return counter.load(std::memory_order_acquire) == 0; });
}