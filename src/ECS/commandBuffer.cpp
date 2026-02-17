/*! \file commandBuffer.cpp
	\brief Contains the function definitions for creating a commandBuffer
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/commandBuffer.h"

#include <cassert>

#include "ECS/ecs.h"

namespace Dimensia::ECS
{

	void CommandBuffer::setParent(Entity child, Entity parent)
	{
		auto buf = getThreadBuffer();
		buf->commands.push_back(Command::makeSetParent(child, parent));
	}

	void CommandBuffer::destroy(Entity entity)
	{
		auto buf = getThreadBuffer();
		buf->commands.push_back(Command::makeDestroy(entity));
	}

	CommandBuffer::ThreadBuffer *CommandBuffer::getThreadBuffer()
	{
		thread_local ThreadBuffer *tls = nullptr;
		if (tls != nullptr)
		{
			return tls;
		}

		// slow path once per thread
		const std::scoped_lock lock(map_mutex_);
		auto &slot = buffers_[std::this_thread::get_id()];
		if (!slot)
		{
			slot = std::make_unique<ThreadBuffer>();
		}
		tls = slot.get();
		return tls;
	}

	void CommandBuffer::Command::destroyBuffer()
	{
		if (type == CmdType::AddComponent)
		{
			ComponentInfos[data.add.compId].destructor(data.add.buffer);
		}
	}

	void CommandBuffer::clear()
	{
		const std::scoped_lock<std::mutex> lock(map_mutex_);
		buffers_.clear();
	}

	template <typename... Ts>
	void CommandBuffer::dispatchAddImpl(ECS &ecs, Entity e, ComponentTypeID id, void *buffer, std::tuple<Ts...>)
	{
		bool handled = false;
		(
			[&] {
				if (componentId<Ts>() == id)
				{
					Ts &value = *reinterpret_cast<Ts *>(buffer);
					ecs.addComponent(e, std::move(value));
					handled = true;
				}
			}(),
			...);
		assert(handled && "Unknown component ID in CommandBuffer::apply");
	}

	void CommandBuffer::dispatchAdd(ECS &ecs, Entity e, ComponentTypeID id, void *buffer)
	{
		dispatchAddImpl(ecs, e, id, buffer, Registry::ComponentTypes{});
	}

	void CommandBuffer::dispatchRemove(ECS &ecs, Entity e, ComponentTypeID id)
	{
		ecs.removeComponent(e, id);
	}

	void CommandBuffer::apply(ECS &ecs)
	{
		std::vector<std::unique_ptr<ThreadBuffer>> local_buffers;
		{
			const std::scoped_lock<std::mutex> lock(map_mutex_);
			for (auto &[_, buf] : buffers_)
			{
				local_buffers.push_back(std::move(buf));
			}
			buffers_.clear();
		}

		for (auto &buf : local_buffers)
		{
			for (auto &cmd : buf->commands)
			{
				switch (cmd.type)
				{
					case CmdType::AddComponent:
						dispatchAdd(ecs, cmd.entity, cmd.data.add.compId, cmd.data.add.buffer);
						cmd.destroyBuffer();
						break;
					case CmdType::RemoveComponent:
						dispatchRemove(ecs, cmd.entity, cmd.data.remove.compId);
						break;
					case CmdType::Destroy:
						ecs.destroyEntity(cmd.entity);
						break;
					case CmdType::SetParent:
						ecs.setParent(cmd.entity, cmd.data.setParent.parent);
						break;
					default:
						break;
				}
			}
		}
	}
} // namespace Dimensia::ECS