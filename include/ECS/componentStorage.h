/*! @file componentStorage.h
	@brief Aligned, type-erased storage for a single ECS component instance.
	@details Declares `ComponentStorage`, a movable, non-copyable owner for exactly one component instance stored as a type-erased pointer
   plus a custom deleter and runtime `ComponentTypeID`. The API offers creation via `ComponentStorage::create<T>()`, accessors for the raw
   pointer and type id, and noexcept move semantics.
	@date 02/19/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMPONENTSTORAGE_H
#define INCLUDE_ECS_COMPONENTSTORAGE_H

#include <memory>

#include "Core/attributeMacros.h"
#include "ECS/componentRegistry.h"

namespace Dimensia::ECS
{
	using Dimensia::Registry::componentID;
	using Dimensia::Registry::ComponentTypeID;

	class ComponentStorage
	{
		public:
			// MARK: Constructor, Assignment Operators, and Destructor

			/*! @brief Default-construct an empty storage.
			 */
			explicit ComponentStorage() noexcept : mPtr(nullptr), mDeleter(nullptr), mTypeID(0) {}

			/*! @brief Construct storage from a raw pointer, deleter and component type id.
				@param[in] ptr Pointer to the component instance. Must be either a null pointer or point to a valid object allocated with an
			   allocation strategy compatible with @p deleter.
				@param[in] deleter Function that will destroy and deallocate the object pointed to by @p ptr. May be `nullptr`.
				@param[in] componentTypeID Runtime component type identifier returned by @ref Dimensia::Registry::componentID "componentID".
				@post Storage takes ownership of `ptr` and will invoke `deleter(ptr)` when destroyed.
			*/
			explicit ComponentStorage(void *ptr, void (*deleter)(void *) noexcept, const ComponentTypeID componentTypeID) noexcept
				: mPtr(ptr), mDeleter(deleter), mTypeID(componentTypeID)
			{}

			/*! @brief Copy construction/assignment is disabled.
				@details `ComponentStorage` manages unique ownership of a component pointer and its deleter, so copying would lead to
			   double-destruction. Use move semantics (`ComponentStorage(ComponentStorage&&)` or `operator=(ComponentStorage&&)`) to
			   transfer ownership instead.
			*/
			ComponentStorage(const ComponentStorage &) = delete;

			/*! @brief Move-assign from @p other.
				@details Transfers ownership of the contained pointer and deleter from @p other into `*this`. If `*this` already owns an
			   object, it is destroyed via the current deleter before taking ownership of the new pointer.
				@param[in,out] other Source storage whose ownership is moved; after the call `other.getPtr()` is `nullptr`.
				@return Reference to `*this`.
			*/
			ComponentStorage &operator=(ComponentStorage &&other) noexcept;

			/*! @brief Move-construct from @p other.
				@details Transfers ownership from @p other into the new storage object. After construction `other.getPtr()` is `nullptr`.
			*/
			ComponentStorage(ComponentStorage &&other) noexcept;

			/*! @brief Copy assignment is disabled.
				@details See the copy-construction comment: ownership of the stored pointer is unique and cannot be copied.
			*/
			ComponentStorage &operator=(const ComponentStorage &) = delete;

			/*! @brief Destroy the stored component if present.
				@details Invokes the stored deleter on the owned pointer if it is non-null, then leaves the storage empty.
			*/
			~ComponentStorage() noexcept;

			// MARK: Getters

			/*! @brief Return the runtime component type id for the stored object.
				@return The `ComponentTypeID` associated with the stored object, or `0` when storage is empty.
			*/
			ATTR_NODISCARD constexpr ComponentTypeID getTypeID() const noexcept
			{
				return mTypeID;
			}

			/*! @brief Return the raw pointer to the stored object.
				@note Callers must not delete the returned pointer; destruction is managed by `ComponentStorage`.
				@return Pointer to the stored component (may be `nullptr` if empty). The returned pointer is non-owning.
			*/
			ATTR_NODISCARD void *getPtr() const noexcept;

			// MARK: Template Member Function

			/*! @brief Allocate and construct a component instance of type `T`, returning owning `ComponentStorage`.
				@tparam T The component type to allocate. `T` must be destructible.
				@param[in] value Value or arguments forwarded to `T`'s constructor.
				@details The function performs an aligned allocation using placement new with `std::align_val_t(alignof(T))`, constructs a
			   `T` with `std::forward<T>(value)`, and returns a `ComponentStorage` that owns the resulting object. A custom deleter is
			   provided which calls the object's destructor and deallocates with the matching aligned deallocation. Ownership is transferred
			   into the returned `ComponentStorage` (the intermediate unique_ptr releases ownership).
				@note The returned storage is responsible for object destruction; callers must not free the pointer.
				@return A `ComponentStorage` owning the newly-constructed component.
			*/
			template <typename T>
			static ComponentStorage create(T &&value)
			{
				/*! @struct Deleter include/ECS/componentStorage.h
					@brief RAII-style deleter used by the temporary `unique_ptr` during allocation.
					@tparam T The component type constructed by `create<T>()`.
					@details Calls the destructor of the pointed-to `T` and deallocates the memory using the matching aligned deallocation
				   (`operator delete` with `std::align_val_t(alignof(T))`). The call operator is `noexcept` to ensure it can safely be used
				   by `std::unique_ptr` during stack-unwinding or normal flow.
				*/
				struct Deleter
				{
					public:
						/*! @brief Destroy and deallocate a `T` instance.
							@param[in,out] ptr Pointer to the object to destroy; may be `nullptr`.
							@details If @p ptr is non-null, invokes the destructor of `T` and deallocates the memory using the aligned
						   deallocation that matches the allocation strategy used in `create<T>()`.
							@note The operation is `noexcept` to allow safe use by RAII helpers such as `std::unique_ptr` during unwinding
						   or normal destruction.
						*/
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
										 componentID<T>()};

				owner.release(); // storage now owns the object
				return storage;
			}

		private:
			/*! @var mPtr
				@brief Non-owning pointer to the stored component instance.
				@details When non-null, `mPtr` points to an object owned by this `ComponentStorage` and must be destroyed with `mDeleter`.
			*/
			void *mPtr;

			/*! @var mDeleter
				@brief Function used to destroy and deallocate the object referenced by `mPtr`.
				@details The deleter must be noexcept and compatible with the allocation used to create the object.
			*/
			void (*mDeleter)(void *) noexcept;

			/*! @var mTypeID
				@brief Runtime component type identifier associated with the stored object.
				@details When `mPtr` is null, `mTypeID` is zero.
			*/
			ComponentTypeID mTypeID;
	};

} // namespace Dimensia::ECS

#endif