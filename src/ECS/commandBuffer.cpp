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
			const std::scoped_lock lock(mMapMutex);
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
		const std::scoped_lock<std::mutex> lock(mMapMutex);
		mBuffers.clear();
	}

	// MARK: Private Member Functions

	ThreadBuffer *CommandBuffer::getThreadBuffer()
	{
		thread_local ThreadBuffer *tls = nullptr;
		if (tls != nullptr)
		{
			return tls;
		}

		// slow path once per thread
		const std::scoped_lock lock(mMapMutex);
		auto &slot = mBuffers[std::this_thread::get_id()];
		if (!slot)
		{
			slot = std::make_unique<ThreadBuffer>();
		}
		tls = slot.get();
		return tls;
	}

	void CommandBuffer::processAdd(ECS &ecs, const Entity &entity, const AddData &addData)
	{
		const auto &infos{Dimensia::Registry::ComponentInfos};
		const ComponentTypeID componentTypeID{addData.storage.getTypeID()};

		assert(componentTypeID < infos.size());

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		const auto &info{infos[componentTypeID]};

		assert(info.addFunc != nullptr && "No addFunc registered for this component type");

		info.addFunc(&ecs, entity, static_cast<std::byte *>(addData.storage.getPtr()));
		// info.destructor(buffer);
	}

	void CommandBuffer::dispatchRemove(ECS &ecs, const Entity &entity, const ComponentTypeID componentTypeID)
	{
		ecs.removeComponent(entity, componentTypeID);
	}
} // namespace Dimensia::ECS