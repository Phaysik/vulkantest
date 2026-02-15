/*! \file component.h
	\brief Contains the function declarations for creating a Component
	\date 02/12/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMPONENT_H
#define INCLUDE_ECS_COMPONENT_H

#include <atomic>
#include <chrono>
#include <string>
#include <string_view>

#include "Core/attributeMacros.h"
#include "ECS/entity.h"

namespace Dimensia::ECS
{
	class ComponentTypeIdSystem
	{
		public:
			template <typename T>
			static std::size_t getTypeId() noexcept
			{
				static const std::size_t typeId{s_nextTypeId.fetch_add(1, std::memory_order_relaxed)};
				return typeId;
			}

		private:
			inline static std::atomic_size_t s_nextTypeId{0};
	};

	class Component
	{
		public:
			explicit Component(std::string_view componentName = "Component") : mName(componentName) {}

			Component(const Component &other) = default;
			Component &operator=(const Component &other) = default;

			virtual ~Component() = default;

			virtual void initialize() {}

			virtual void update(ATTR_MAYBE_UNUSED std::chrono::milliseconds deltaTime) {}

			virtual void render() {}

			constexpr void setOwner(Entity *entity)
			{
				mOwner = entity;
			}

			ATTR_NODISCARD constexpr Entity *getOwner() const
			{
				return mOwner;
			}

			template <typename T>
			static std::size_t getTypeId() noexcept
			{
				return ComponentTypeIdSystem::getTypeId<T>();
			}

			// Runtime-stored type id for this component instance. Set by Entity when added.
			constexpr void setTypeId(std::size_t typeID) noexcept
			{
				mTypeId = typeID;
			}

			ATTR_NODISCARD constexpr std::size_t getTypeId() const noexcept
			{
				return mTypeId;
			}

		private:
			Entity *mOwner{nullptr};
			std::string mName;
			std::size_t mTypeId{static_cast<std::size_t>(-1)};
	};
} // namespace Dimensia::ECS

#endif