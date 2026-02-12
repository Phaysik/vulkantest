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
					const std::size_t index = iterator->second;
					return static_cast<T *>(mComponents.at(index).get());
				}

				// Create new component
				auto component{std::make_unique<T>(std::forward<Args>(args)...)};

				T *componentPtr{component.get()};
				componentPtr->setOwner(this);

				mComponents.emplace_back(std::move(component));
				// record the component's type id in the parallel vector
				mComponentTypeIds.emplace_back(typeId);

				const std::size_t index = mComponents.size() - 1;
				// Cache the component index by its compile-time type id
				mComponentCache.emplace(typeId, index);

				return componentPtr;
			}

			template <typename T>
			ATTR_NODISCARD T *getComponent() noexcept
			{
				const std::size_t typeId{Component::getTypeId<T>()};

				const auto iterator = mComponentCache.find(typeId);
				if (iterator != mComponentCache.end())
				{
					const std::size_t index = iterator->second;
					return static_cast<T *>(mComponents.at(index).get());
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
					const std::size_t index = iterator->second;
					return static_cast<const T *>(mComponents.at(index).get());
				}

				return nullptr;
			}

			template <typename T>
			bool removeComponent()
			{
				const std::size_t typeId{Component::getTypeId<T>()};

				auto iterator{mComponentCache.find(typeId)};
				if (iterator == mComponentCache.end())
				{
					return false;
				}

				const std::size_t index{iterator->second};
				// remove mapping for this type
				mComponentCache.erase(iterator);

				const std::size_t lastIndex{mComponents.size() - 1};
				if (index != lastIndex)
				{
					// move last element into the removed slot
					std::swap(mComponents.at(index), mComponents.at(lastIndex));
					std::swap(mComponentTypeIds.at(index), mComponentTypeIds.at(lastIndex));

					// update cache entry for the moved component's type id
					const std::size_t movedTypeId{mComponentTypeIds.at(index)};
					mComponentCache[movedTypeId] = index;
				}

				// pop the last element (the removed component)
				mComponents.pop_back();
				mComponentTypeIds.pop_back();

				return true;
			}

		private:
			std::string mName;
			bool mActive{true};
			std::vector<std::unique_ptr<Component>> mComponents;
			// Parallel vector storing the compile-time type id for each entry in mComponents.
			std::vector<std::size_t> mComponentTypeIds;
			// Map from component type id -> index within mComponents / mComponentTypeIds
			std::unordered_map<std::size_t, std::size_t> mComponentCache;
	};
} // namespace Dimensia::ECS

#endif