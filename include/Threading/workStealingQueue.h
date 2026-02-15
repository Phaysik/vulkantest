/*! \file workStealingQueue.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_WORKSTEALINGQUEUE_H
#define INCLUDE_ECS_WORKSTEALINGQUEUE_H

#include <deque>
#include <functional>
#include <mutex>

namespace Dimensia::Threading
{
	class WorkStealingQueue
	{
		public:
			using Task = std::function<void()>;

			void push(Task task);
			bool try_pop(Task &task);
			bool try_steal(Task &task);
			size_t size() const;

		private:
			std::deque<Task> queue;
			mutable std::mutex mutex;
	};
} // namespace Dimensia::Threading

#endif