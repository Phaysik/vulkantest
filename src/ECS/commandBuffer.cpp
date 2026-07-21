/*! \file commandBuffer.cpp
	\brief Contains the function definitions for creating a commandBuffer
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/commandBuffer.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <vector>

#include "ECS/command.h"
#include "ECS/componentRegistry.h"
#include "ECS/ecs.h"
#include "ECS/entity.h"

namespace Dimensia::ECS
{
	// MARK: Public Member Functions

	void CommandBuffer::setParent(const Entity &child, const Entity &parent)
	{
		ThreadBuffer *buf{getThreadBuffer()};
		buf->commands.push_back(Command::makeSetParent(child, parent));
	}

	void CommandBuffer::destroy(const Entity &entity)
	{
		ThreadBuffer *buf{getThreadBuffer()};
		buf->commands.push_back(Command::makeDestroy(entity));
	}

	void CommandBuffer::apply(ECS &ecs)
	{
		std::vector<std::vector<Command>> local_command_lists;
		{
			const std::unique_lock lock(mMapMutex);
			local_command_lists.reserve(mBuffers.size());
			for (auto &[threadID, buf] : mBuffers)
			{
				// Move the commands vector out – buf->commands becomes empty.
				local_command_lists.push_back(std::move(buf->commands));
				// The ThreadBuffer itself stays in mBuffers, ready for reuse.
			}
		}

		for (auto &commands : local_command_lists)
		{
			if (commands.empty())
			{
				continue;
			}

			// ---- 1️⃣ Partition Adds to front ----
			// NOLINTBEGIN(modernize-use-ranges,boost-use-ranges,llvm-use-ranges)
			auto addEnd{std::partition(commands.begin(), commands.end(),
									   [](const Command &command) { return command.getType() == CmdType::AddComponent; })};
			// NOLINTEND(modernize-use-ranges,boost-use-ranges,llvm-use-ranges)

			// ---- Process Adds ----
			for (auto it{commands.begin()}; it != addEnd; ++it)
			{
				auto &cmd{*it};
				auto &addData{std::get<AddData>(cmd.getData())};

				processAdd(ecs, cmd.getEntity(), addData);
			}

			// ---- 2️⃣ Partition Removes in remaining range ----
			auto removeEnd{std::partition(addEnd, commands.end(),
										  [](const Command &command) { return command.getType() == CmdType::RemoveComponent; })};

			// ---- Process Removes ----
			for (auto it{addEnd}; it != removeEnd; ++it)
			{
				auto &cmd{*it};
				const auto &removeData{std::get<RemoveData>(cmd.getData())};

				dispatchRemove(ecs, cmd.getEntity(), removeData.compId);
			}

			// ---- 3️⃣ Partition Destroy in remaining range ----
			auto destroyEnd{
				std::partition(removeEnd, commands.end(), [](const Command &command) { return command.getType() == CmdType::Destroy; })};

			// ---- Process Destroy ----
			for (auto it{removeEnd}; it != destroyEnd; ++it)
			{
				ecs.destroyEntity(it->getEntity());
			}

			// ---- 4️⃣ Remaining are SetParent ----
			for (auto it{destroyEnd}; it != commands.end(); ++it)
			{
				const auto &setParentData{std::get<SetParentData>(it->getData())};

				ecs.setParent(it->getEntity(), setParentData.parent);
			}
		}
	}

	void CommandBuffer::clear()
	{
		const std::unique_lock lock(mMapMutex);

		for (auto &[threadID, buf] : mBuffers)
		{
			buf->commands.clear();
		}
	}

	// MARK: Private Member Functions

	ThreadBuffer *CommandBuffer::getThreadBuffer()
	{
		// Fast path: thread_local caches the buffer pointer to avoid locking on every call
		thread_local ThreadBuffer *cachedBuffer{nullptr};
		const thread_local CommandBuffer *cachedOwner{nullptr};

		if (cachedOwner == this && cachedBuffer != nullptr)
		{
			return cachedBuffer;
		}

		// Slow path: check if this thread already has a buffer under a shared lock
		const auto threadId{std::this_thread::get_id()};

		{
			const std::shared_lock readLock(mMapMutex);
			auto iterator{mBuffers.find(threadId)};

			if (iterator != mBuffers.end())
			{
				cachedBuffer = iterator->second.get();
				cachedOwner = this;
				return cachedBuffer;
			}
		}

		// Registration path: take exclusive lock to insert a new buffer
		const std::unique_lock writeLock(mMapMutex);
		auto &slot{mBuffers[threadId]};

		if (!slot)
		{
			slot = std::make_unique<ThreadBuffer>();
		}

		cachedBuffer = slot.get();
		cachedOwner = this;
		return cachedBuffer;
	}

	void CommandBuffer::processAdd(ECS &ecs, const Entity &entity, const AddData &addData)
	{
		const auto &infos{Dimensia::Registry::ComponentInfos};
		const ComponentTypeID componentTypeID{addData.storage.getTypeID()};

		assert(componentTypeID < infos.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const Dimensia::Registry::ComponentInfo &info{infos[componentTypeID]};

		assert(info.addFunc != nullptr && "No addFunc registered for this component type");

		info.addFunc(&ecs, entity, static_cast<std::byte *>(addData.storage.getPtr()));
	}

	void CommandBuffer::dispatchRemove(ECS &ecs, const Entity &entity, const ComponentTypeID componentTypeID)
	{
		ecs.removeComponent(entity, componentTypeID);
	}
} // namespace Dimensia::ECS