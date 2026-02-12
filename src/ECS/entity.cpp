/*! \file entity.cpp
	\brief Contains the function definitions for creating a entity
	\date 02/12/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#include "ECS/entity.h"

#include <chrono>
#include <string_view>

namespace Dimensia::ECS
{
	Entity::Entity(std::string_view entityName) : mName(entityName), mComponents(0), mComponentTypeIds(0), mComponentCache(0)
	{
		mComponents.reserve(4);
		mComponentTypeIds.reserve(4);
		mComponentCache.reserve(4);
	}

	const std::string &Entity::getName() const noexcept
	{
		return mName;
	}

	void Entity::initialize()
	{
		for (const auto &component : mComponents)
		{
			component->initialize();
		}
	}

	void Entity::update(const std::chrono::milliseconds deltaTime)
	{
		if (!mActive)
		{
			return;
		}

		for (const auto &component : mComponents)
		{
			component->update(deltaTime);
		}
	}

	void Entity::render()
	{
		if (!mActive)
		{
			return;
		}

		for (const auto &component : mComponents)
		{
			component->render();
		}
	}
} // namespace Dimensia::ECS