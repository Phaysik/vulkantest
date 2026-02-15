/*! \file latch.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_LATCH_H
#define INCLUDE_ECS_LATCH_H

#include <atomic>
#include <condition_variable>
#include <mutex>

class Latch
{
	public:
		explicit Latch(int count);
		void count_down();
		void wait();

	private:
		std::atomic<int> counter;
		std::mutex mutex;
		std::condition_variable cv;
};

#endif