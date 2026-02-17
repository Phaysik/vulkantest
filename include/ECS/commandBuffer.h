/*! \file commandBuffer.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMMANDBUFFER_H
#define INCLUDE_ECS_COMMANDBUFFER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "componentRegistry.h"
#include "entity.h"

namespace Dimensia::ECS
{

	// Forward declaration
	class ECS;

	using Registry::componentId;
	using Registry::ComponentTypeID;

	class CommandBuffer
	{
		public:
			template <typename T>
			void addComponent(Entity entity, T value)
			{
				auto buf = getThreadBuffer();
				buf->commands.push_back(Command::makeAdd(entity, std::move(value)));
			}

			template <typename T>
			void removeComponent(Entity entity)
			{
				auto buf = getThreadBuffer();
				buf->commands.push_back(Command::makeRemove(entity, componentId<T>()));
			}

			void destroy(Entity entity);
			void setParent(Entity child, Entity parent);
			void apply(ECS &ecs);
			void clear();

		private:
			enum class CmdType : uint8_t
			{
				AddComponent,
				RemoveComponent,
				Destroy,
				SetParent
			};

			struct Command
			{
					CmdType type{};
					Entity entity{};

					union {
							struct
							{
									ComponentTypeID compId;
									alignas(Registry::MAX_COMPONENT_ALIGN) std::byte buffer[Registry::MAX_COMPONENT_SIZE];
							} add;

							struct
							{
									ComponentTypeID compId;
							} remove;

							struct
							{
									Entity parent;
							} setParent;
					} data{};

					template <typename T>
					static Command makeAdd(Entity e, T &&value)
					{
						Command cmd;
						cmd.type = CmdType::AddComponent;
						cmd.entity = e;
						cmd.data.add.compId = componentId<T>();
						new (cmd.data.add.buffer) T(std::forward<T>(value));
						return cmd;
					}

					static Command makeRemove(Entity e, ComponentTypeID compId)
					{
						Command cmd;
						cmd.type = CmdType::RemoveComponent;
						cmd.entity = e;
						cmd.data.remove.compId = compId;
						return cmd;
					}

					static Command makeDestroy(Entity e)
					{
						Command cmd;
						cmd.type = CmdType::Destroy;
						cmd.entity = e;
						return cmd;
					}

					static Command makeSetParent(Entity child, Entity parent)
					{
						Command cmd;
						cmd.type = CmdType::SetParent;
						cmd.entity = child;
						cmd.data.setParent.parent = parent;
						return cmd;
					}

					void destroyBuffer();
			};

			struct ThreadBuffer
			{
					std::vector<Command> commands;
			};

			std::unordered_map<std::thread::id, std::unique_ptr<ThreadBuffer>> buffers_;
			std::mutex map_mutex_;

			ThreadBuffer *getThreadBuffer();

			template <typename... Ts>
			static void dispatchAddImpl(ECS &ecs, Entity e, ComponentTypeID id, void *buffer, std::tuple<Ts...>);
			static void dispatchAdd(ECS &ecs, Entity e, ComponentTypeID id, void *buffer);
			static void dispatchRemove(ECS &ecs, Entity e, ComponentTypeID id);
	};
} // namespace Dimensia::ECS

#endif