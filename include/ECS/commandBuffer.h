/*! \file commandBuffer.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMMANDBUFFER_H
#define INCLUDE_ECS_COMMANDBUFFER_H

#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "command.h"
#include "componentRegistry.h"
#include "entity.h"

namespace Dimensia::ECS
{
	// Forward declaration
	class ECS;

	using Registry::componentId;
	using Registry::ComponentTypeID;

	struct ThreadBuffer
	{
		public:
			std::vector<Command> commands;
	};

	class CommandBuffer
	{
		public:
			// MARK: Public Member Functions

			void setParent(const Entity &child, const Entity &parent);

			void destroy(const Entity &entity);

			void apply(ECS &ecs);

			void clear();

			// MARK: Template Member Functions

			template <typename T>
			void addComponent(const Entity &entity, T &value)
			{
				ThreadBuffer *buf{getThreadBuffer()};

				buf->commands.push_back(Command::makeAdd(entity, std::move(value)));
			}

			template <typename T>
			void addComponent(const Entity &entity, T &&value)
			{
				ThreadBuffer *buf{getThreadBuffer()};

				buf->commands.push_back(Command::makeAdd(entity, std::forward<T>(value)));
			}

			template <typename T>
			void removeComponent(const Entity &entity)
			{
				ThreadBuffer *buf{getThreadBuffer()};

				buf->commands.push_back(Command::makeRemove(entity, componentId<T>()));
			}

		private:
			std::unordered_map<std::thread::id, std::unique_ptr<ThreadBuffer>> mBuffers;
			std::mutex mMapMutex;

			// MARK: Private Member Functions

			ThreadBuffer *getThreadBuffer();

			template <typename... Ts>
			static void dispatchAddImpl(ECS &ecs, const Entity &entity, const ComponentTypeID componentTypeID, std::byte *buffer,
										std::tuple<Ts...> /* componentTypes */);
			static void dispatchAdd(ECS &ecs, const Entity &entity, const ComponentTypeID componentTypeID, std::byte *buffer);
			static void dispatchRemove(ECS &ecs, const Entity &entity, const ComponentTypeID componentTypeID);
	};
} // namespace Dimensia::ECS

#endif