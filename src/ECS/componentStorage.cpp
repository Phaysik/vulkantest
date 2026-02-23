/*! \file componentStorage.cpp
	\brief Contains the function definitions for creating a storage container for a component
	\date 02/19/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/componentStorage.h"

#include "Core/attributeMacros.h"

namespace Dimensia::ECS
{
	// MARK: Constructor, Assignment Operators, and Destructor

	ComponentStorage &ComponentStorage::operator=(ComponentStorage &&other) noexcept
	{
		if (this != &other)
		{
			if (mPtr != nullptr)
			{
				mDeleter(mPtr);
			}

			mPtr = other.mPtr;
			mDeleter = other.mDeleter;
			mTypeID = other.mTypeID;

			other.mPtr = nullptr;
			other.mDeleter = nullptr;
			other.mTypeID = 0;
		}

		return *this;
	}

	ComponentStorage::ComponentStorage(ComponentStorage &&other) noexcept
		: mPtr(other.mPtr), mDeleter(other.mDeleter), mTypeID(other.mTypeID)
	{
		other.mPtr = nullptr;
		other.mDeleter = nullptr;
		other.mTypeID = 0;
	}

	ComponentStorage::~ComponentStorage() noexcept
	{
		if (mPtr != nullptr)
		{
			mDeleter(mPtr);
		}
	}

	// MARK: Getters

	ATTR_NODISCARD void *ComponentStorage::getPtr() const noexcept
	{
		return mPtr;
	}
} // namespace Dimensia::ECS