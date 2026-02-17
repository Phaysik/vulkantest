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
#include <latch>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Dimensia::Threading
{
	class ThreadPool
	{
		public:
			// MARK: Constructors, Destructor, and Assignment Operators

			explicit ThreadPool(const std::size_t numThreads = std::thread::hardware_concurrency());

			ThreadPool(const ThreadPool &other) = delete;
			ThreadPool(ThreadPool &&other) noexcept = delete;
			ThreadPool &operator=(const ThreadPool &other) = delete;
			ThreadPool &operator=(ThreadPool &&other) noexcept = delete;

			~ThreadPool();

			// MARK: Member Function

			void submit_with_latch(std::function<void()> &&task, std::latch &latch) const;

			// MARK: Template Member Function

			template <typename F>
			std::future<std::invoke_result_t<F>> submit(F &&func) const
			{
				using return_type = std::invoke_result_t<F>;
				auto task{std::make_shared<std::packaged_task<return_type()>>(std::forward<F>(func))};

				std::future<return_type> result = task->get_future();
				{
					const std::unique_lock<std::mutex> lock(mQueueMutex);
					mTasks.emplace([task]() { (*task)(); });
				}

				mCondition.notify_one();
				return result;
			}

		private:
			mutable std::queue<std::function<void()>> mTasks;
			mutable std::mutex mQueueMutex;
			mutable std::condition_variable mCondition;
			std::vector<std::thread> mWorkers;
			std::atomic<bool> mStop;
	};
} // namespace Dimensia::Threading
#endif