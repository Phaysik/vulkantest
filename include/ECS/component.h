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

namespace Dimensia::ECS
{
	class Entity; // Forward-declare the Entity type in the same namespace

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

	// NOLINTBEGIN(cppcoreguidelines-special-member-functions,hicpp-special-member-functions)
	class Component
	// NOLINTEND(cppcoreguidelines-special-member-functions,hicpp-special-member-functions)
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

		private:
			Entity *mOwner{nullptr};
			std::string mName;
	};
} // namespace Dimensia::ECS

#endif