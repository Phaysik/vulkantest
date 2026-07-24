/*! \file commandBuffer.cpp
	\brief Contains the function definitions for creating a commandBuffer
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/commandBuffer.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <vector>

#include "Core/typedefs.h"
#include "ECS/command.h"
#include "ECS/componentRegistry.h"
#include "ECS/ecs.h"
#include "ECS/entity.h"

namespace Dimensia::ECS
{
	// MARK: Public Member Functions

	void CommandBuffer::setParent(const Entity &child, const Entity &parent)
	{
		record(Command::makeSetParent(child, parent));
	}

	void CommandBuffer::destroy(const Entity &entity)
	{
		record(Command::makeDestroy(entity));
	}

	void CommandBuffer::apply(ECS &ecs)
	{
		const auto commandPhase = [](const CmdType type) constexpr noexcept -> Dimensia::Core::ub {
			switch (type)
			{
				case CmdType::AddComponent:
					return 0;
				case CmdType::RemoveComponent:
					return 1;
				case CmdType::Destroy:
					return 2;
				case CmdType::SetParent:
					return 3;
				default:
					return 4;
			}
		};

		const ECS::StructuralGuard structuralGuard{ecs.mStructuralMutex};
		const std::scoped_lock consumerLock(mConsumerMutex);
		std::vector<Command> commands;
		{
			const std::unique_lock lock(mMapMutex);
			for (auto &[threadID, buf] : mBuffers)
			{
				const std::scoped_lock bufferLock(buf->mutex);
				commands.insert(commands.end(), std::make_move_iterator(buf->commands.begin()),
								std::make_move_iterator(buf->commands.end()));
				buf->commands.clear();
			}
		}

		std::ranges::sort(commands, [commandPhase](const Command &left, const Command &right) {
			const auto leftPhase{commandPhase(left.getType())};
			const auto rightPhase{commandPhase(right.getType())};
			return (leftPhase < rightPhase) || (leftPhase == rightPhase && left.getSequence() < right.getSequence());
		});

		for (Command &command : commands)
		{
			switch (command.getType())
			{
				case CmdType::AddComponent:
					processAdd(ecs, command.getEntity(), std::get<AddData>(command.getData()));
					break;
				case CmdType::RemoveComponent:
					dispatchRemove(ecs, command.getEntity(), std::get<RemoveData>(command.getData()).compId);
					break;
				case CmdType::Destroy:
					ecs.destroyEntity(command.getEntity(), true);
					break;
				case CmdType::SetParent:
					ecs.setParent(command.getEntity(), std::get<SetParentData>(command.getData()).parent);
					break;
				default:
					break;
			}
		}
	}

	void CommandBuffer::clear()
	{
		const std::scoped_lock consumerLock(mConsumerMutex);
		const std::unique_lock lock(mMapMutex);

		for (auto &[threadID, buf] : mBuffers)
		{
			const std::scoped_lock bufferLock(buf->mutex);
			buf->commands.clear();
		}
	}

	// MARK: Private Member Functions

	ThreadBuffer *CommandBuffer::getThreadBuffer()
	{
		// Fast path: thread_local caches the buffer pointer to avoid locking on every call.
		// Uses a unique instance ID instead of a raw pointer to prevent ABA problems
		// when a CommandBuffer is destroyed and a new one is allocated at the same address.
		thread_local ThreadBuffer *cachedBuffer{nullptr};
		thread_local uint32_t cachedInstanceID{0};

		if (cachedInstanceID == mInstanceID && cachedBuffer != nullptr)
		{
			return cachedBuffer;
		}

		// Slow path: check if this thread already has a buffer under a shared lock
		const auto threadId{std::this_thread::get_id()};

		{
			const std::shared_lock readLock(mMapMutex);
			const auto iterator{mBuffers.find(threadId)};

			if (iterator != mBuffers.end())
			{
				cachedBuffer = iterator->second.get();
				cachedInstanceID = mInstanceID;
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
		cachedInstanceID = mInstanceID;
		return cachedBuffer;
	}

	void CommandBuffer::record(Command command)
	{
		ThreadBuffer *const buf{getThreadBuffer()};
		const std::scoped_lock lock(buf->mutex);
		command.setSequence(mNextSequence.fetch_add(1, std::memory_order_relaxed));
		buf->commands.push_back(std::move(command));
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