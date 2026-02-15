/*! \file threadPool.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_THREADPOOL_H
#define INCLUDE_ECS_THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "latch.h"

namespace Dimensia::Threading
{
	class ThreadPool
	{
		public:
			explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
			~ThreadPool();

			template <typename F>
			auto submit(F &&f) const -> std::future<decltype(f())>
			{
				using return_type = decltype(f());
				auto task = std::make_shared<std::packaged_task<return_type()>>(std::forward<F>(f));
				std::future<return_type> result = task->get_future();
				{
					std::unique_lock<std::mutex> lock(queueMutex);
					tasks.emplace([task]() { (*task)(); });
				}
				condition.notify_one();
				return result;
			}

			void submit_with_latch(std::function<void()> task, Latch &latch) const;

		private:
			mutable std::queue<std::function<void()>> tasks;
			mutable std::mutex queueMutex;
			mutable std::condition_variable condition;
			std::vector<std::thread> workers;
			std::atomic<bool> stop;
	};
} // namespace Dimensia::Threading
#endif