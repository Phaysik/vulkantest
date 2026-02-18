/*! \file workStealingPool.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_WORKSTEALINGPOOL_H
#define INCLUDE_ECS_WORKSTEALINGPOOL_H

#include <atomic>
#include <functional>
#include <latch>
#include <thread>
#include <vector>

#include "ECS/archetype.h"

#include "workStealingQueue.h"

namespace Dimensia::Threading
{
	using ECS::Archetype;

	class WorkStealingPool
	{
		public:
			// MARK: Constructor, Destructor, and Assignment Operators

			explicit WorkStealingPool(const std::size_t numThreads = std::thread::hardware_concurrency());

			WorkStealingPool(const WorkStealingPool &other) = delete;
			WorkStealingPool(WorkStealingPool &&other) noexcept = delete;
			WorkStealingPool &operator=(const WorkStealingPool &other) = delete;
			WorkStealingPool &operator=(WorkStealingPool &&other) noexcept = delete;

			~WorkStealingPool();

			// MARK: Template Function

			template <typename TaskFunc>
			void submit_chunks(const std::vector<std::pair<Archetype *, uint32_t>> &chunks, TaskFunc &&func, std::latch &latch,
							   std::size_t batchSize)
			{
				std::vector<std::pair<Archetype *, uint32_t>> batch;
				batch.reserve(batchSize);

				for (const auto &chunk : chunks)
				{
					batch.push_back(chunk);

					if (batch.size() >= batchSize)
					{
						auto task = [batch, func = std::forward<TaskFunc>(func), &latch]() {
							for (const auto &[arch, idx] : batch)
							{
								func(arch, idx);
							}

							latch.count_down();
						};

						submit_task(std::move(task));
						batch.clear();
					}
				}

				if (!batch.empty())
				{
					auto task = [batch, func = std::forward<TaskFunc>(func), &latch]() {
						for (const auto &[arch, idx] : batch)
						{
							func(arch, idx);
						}

						latch.count_down();
					};

					submit_task(std::move(task));
				}
			}

		private:
			// MARK: Private Member Functions

			void submit_task(std::function<void()> &&task);
			void worker_loop(const std::size_t workerID);

		private:
			std::vector<std::thread> mWorkers;
			std::vector<WorkStealingQueue> mQueues;
			std::atomic<std::size_t> mTaskCount;
			std::atomic<bool> mStop;
	};
} // namespace Dimensia::Threading

#endif