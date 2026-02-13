/*! \file entity.h
	\brief Contains the function declarations for creating an Entity
	\date 02/12/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_ENTITY_H
#define INCLUDE_ECS_ENTITY_H

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "component.h"

namespace Dimensia::ECS
{
	class Entity
	{
		public:
			explicit Entity(std::string_view entityName);

			ATTR_NODISCARD const std::string &getName() const noexcept;

			ATTR_NODISCARD constexpr bool isActive() const noexcept
			{
				return mActive;
			}

			constexpr void setActive(bool isActive) noexcept
			{
				mActive = isActive;
			}

			void initialize();

			void update(const std::chrono::milliseconds deltaTime);

			void render();

			template <typename T, typename... Args>
			ATTR_NODISCARD T *addComponent(Args &&...args)
			{
				static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

				const std::size_t typeId{Component::getTypeId<T>()};

				// Check if component of this type already exists
				const auto iterator{mComponentCache.find(typeId)};
				if (iterator != mComponentCache.end())
				{
					return static_cast<T *>(iterator->second);
				}

				// Create new component
				auto component{std::make_unique<T>(std::forward<Args>(args)...)};

				T *componentPtr{component.get()};
				componentPtr->setOwner(this);

				mComponents.emplace_back(std::move(component));

				// Cache the component pointer by its compile-time type id (most-recent)
				mComponentCache.emplace(typeId, componentPtr);

				return componentPtr;
			}

			template <typename T>
			ATTR_NODISCARD T *getComponent() noexcept
			{
				const std::size_t typeId{Component::getTypeId<T>()};

				const auto iterator = mComponentCache.find(typeId);
				if (iterator != mComponentCache.end())
				{
					return static_cast<T *>(iterator->second);
				}

				return nullptr;
			}

			template <typename T>
			ATTR_NODISCARD const T *getComponent() const noexcept
			{
				const std::size_t typeId{Component::getTypeId<T>()};

				const auto iterator = mComponentCache.find(typeId);
				if (iterator != mComponentCache.end())
				{
					return static_cast<const T *>(iterator->second);
				}

				return nullptr;
			}

			template <typename T>
			bool removeComponent()
			{
				const std::size_t typeId{Component::getTypeId<T>()};

				auto iterator = mComponentCache.find(typeId);
				if (iterator == mComponentCache.end())
				{
					return false;
				}

				const Component *componentPtr{iterator->second};
				mComponentCache.erase(iterator);

				const auto newEnd{
					std::remove_if(mComponents.begin(), mComponents.end(),
								   [componentPtr](const std::unique_ptr<Component> &comp) { return comp.get() == componentPtr; })};

				if (newEnd != mComponents.end())
				{
					mComponents.erase(newEnd, mComponents.end());
					return true;
				}

				return false;
			}

		private:
			std::string mName;
			bool mActive{true};
			std::vector<std::unique_ptr<Component>> mComponents;
			std::unordered_map<std::size_t, Component *> mComponentCache;
	};
} // namespace Dimensia::ECS

#endif