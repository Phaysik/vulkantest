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
#include <thread>
#include <vector>

#include "ECS/archetype.h"

#include "latch.h"
#include "workStealingQueue.h"

namespace Dimensia::Threading
{
	using ECS::Archetype;

	class WorkStealingPool
	{
		public:
			explicit WorkStealingPool(std::size_t numThreads = std::thread::hardware_concurrency());
			~WorkStealingPool();

			template <typename TaskFunc>
			void submit_chunks(const std::vector<std::pair<Archetype *, uint32_t>> &chunks, TaskFunc &&func, Latch &latch, std::size_t batchSize)
			{
				std::vector<std::pair<Archetype *, uint32_t>> batch;
				batch.reserve(batchSize);
				for (const auto &chunk : chunks)
				{
					batch.push_back(chunk);
					if (batch.size() >= batchSize)
					{
						auto task = [batch, func, &latch]() {
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
					auto task = [batch, func, &latch]() {
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
			void submit_task(std::function<void()> task);
			void worker_loop(std::size_t workerId);

			std::vector<std::thread> workers;
			std::vector<WorkStealingQueue> queues;
			std::atomic<bool> stop;
			std::atomic<std::size_t> taskCount;
	};
} // namespace Dimensia::Threading

#endif