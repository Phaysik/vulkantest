/*! \file componentStorage.h
	\brief Contains the function declarations for creating a storage container for a component
	\date 02/19/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMPONENTSTORAGE_H
#define INCLUDE_ECS_COMPONENTSTORAGE_H

#include <memory>

#include "Core/attributeMacros.h"
#include "ECS/componentRegistry.h"

namespace Dimensia::ECS
{
	using Dimensia::Registry::componentId;
	using Dimensia::Registry::ComponentTypeID;

	class ComponentStorage
	{
		public:
			// MARK: Constructor, Assignment Operators, and Destructor

			explicit ComponentStorage() noexcept : mPtr(nullptr), mDeleter(nullptr), mTypeID(0) {}

			explicit ComponentStorage(void *ptr, void (*deleter)(void *) noexcept, const ComponentTypeID componentTypeID) noexcept
				: mPtr(ptr), mDeleter(deleter), mTypeID(componentTypeID)
			{}

			ComponentStorage(const ComponentStorage &) = delete;

			ComponentStorage &operator=(ComponentStorage &&other) noexcept;

			ComponentStorage(ComponentStorage &&other) noexcept;

			ComponentStorage &operator=(const ComponentStorage &) = delete;

			~ComponentStorage() noexcept;

			// MARK: Getters

			ATTR_NODISCARD constexpr ComponentTypeID getTypeID() const noexcept
			{
				return mTypeID;
			}

			ATTR_NODISCARD void *getPtr() const noexcept;

			// MARK: Template Member Function

			template <typename T>
			static ComponentStorage create(T &&value)
			{
				// Custom deleter matching the aligned allocation
				struct Deleter
				{
					public:
						void operator()(T *ptr) const noexcept
						{
							if (ptr)
							{
								ptr->~T();
								operator delete(ptr, std::align_val_t(alignof(T)));
							}
						}
				};

				// Allocate and construct with proper alignment; result immediately owned by unique_ptr
				std::unique_ptr<T, Deleter> owner(new (std::align_val_t(alignof(T))) T(std::forward<T>(value)));

				// Transfer ownership to ComponentStorage
				ComponentStorage storage{owner.get(),
										 [](void *ptr) noexcept {
											 T *tptr{static_cast<T *>(ptr)};
											 tptr->~T();
											 operator delete(ptr, std::align_val_t(alignof(T)));
										 },
										 componentId<T>()};

				owner.release(); // storage now owns the object
				return storage;
			}

		private:
			void *mPtr;						   // pointer to properly aligned storage
			void (*mDeleter)(void *) noexcept; // function that destroys and deallocates
			ComponentTypeID mTypeID;
	};
} // namespace Dimensia::ECS

#endif