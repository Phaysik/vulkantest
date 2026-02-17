/*! \file commandBuffer.cpp
	\brief Contains the function definitions for creating a commandBuffer
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/commandBuffer.h"

#include <cassert>

#include "Core/attributeMacros.h"
#include "ECS/ecs.h"

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
		std::vector<std::unique_ptr<ThreadBuffer>> local_buffers;
		{
			const std::scoped_lock<std::mutex> lock(mMapMutex);
			for (auto &[threadID, buf] : mBuffers)
			{
				local_buffers.push_back(std::move(buf));
			}
			mBuffers.clear();
		}

		for (auto &buf : local_buffers)
		{
			for (auto &cmd : buf->commands)
			{
				switch (cmd.getType())
				{
					case CmdType::AddComponent:
						{
							auto &addData = std::get<AddData>(cmd.getData());
							dispatchAdd(ecs, cmd.getEntity(), addData.compId, addData.buffer.data());
							cmd.destroyBuffer();
							break;
						}
					case CmdType::RemoveComponent:
						{
							const auto &removeData = std::get<RemoveData>(cmd.getData());
							dispatchRemove(ecs, cmd.getEntity(), removeData.compId);
							break;
						}
					case CmdType::Destroy:
						ecs.destroyEntity(cmd.getEntity());
						break;
					case CmdType::SetParent:
						{
							const auto &setParentData = std::get<SetParentData>(cmd.getData());
							ecs.setParent(cmd.getEntity(), setParentData.parent);
							break;
						}
					default:
						break;
				}
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

	template <typename... Ts>
	void CommandBuffer::dispatchAddImpl(ECS &ecs, const Entity &entity, const ComponentTypeID componentTypeID, std::byte *buffer,
										std::tuple<Ts...> /* componentTypes */)
	{
		ATTR_MAYBE_UNUSED bool handled{false};
		(
			[&] {
				if (componentId<Ts>() == componentTypeID)
				{
					// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
					Ts &value = *std::launder(reinterpret_cast<Ts *>(buffer));
					ecs.addComponent(entity, std::move(value));
					handled = true;
				}
			}(),
			...);

		assert(handled && "Unknown component ID in CommandBuffer::apply");
	}

	void CommandBuffer::dispatchAdd(ECS &ecs, const Entity &entity, const ComponentTypeID componentTypeID, std::byte *buffer)
	{
		dispatchAddImpl(ecs, entity, componentTypeID, buffer, Registry::ComponentTypes{});
	}

	void CommandBuffer::dispatchRemove(ECS &ecs, const Entity &entity, const ComponentTypeID componentTypeID)
	{
		ecs.removeComponent(entity, componentTypeID);
	}
} // namespace Dimensia::ECS