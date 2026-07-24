/*! @file command.h
	@brief Commands used to mutate entities and their components.
	@details This header defines command types and the `Command` value object used to enqueue changes to the ECS (add/remove components,
   destroy entities, and set parent relationships). `Command` carries a small discriminated union of payloads and the target `Entity`.
	@date 02/17/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMMAND_H
#define INCLUDE_ECS_COMMAND_H

#include <cstdint>
#include <variant>

#include "Core/attributeMacros.h"
#include "Core/typedefs.h"
#include "ECS/componentStorage.h"

#include "componentRegistry.h"
#include "entity.h"

namespace Dimensia::ECS
{
	using Dimensia::Registry::ComponentTypeID;

	/*! @enum CmdType include/ECS/command.h
		@brief Types of commands supported by the ECS command queue.
		@showenumvalues
	*/
	enum class CmdType : Dimensia::Core::ub
	{
		AddComponent,
		RemoveComponent,
		Destroy,
		SetParent
	};

	/*! @struct AddData include/ECS/command.h
		@brief Payload for `CmdType::AddComponent`.
		@details Holds an owning `ComponentStorage` containing the component value to add.
	*/
	struct AddData
	{
		public:
			ComponentStorage storage;
	};

	/*! @struct RemoveData include/ECS/command.h
		@brief Payload for `CmdType::RemoveComponent`.
		@param[in] compId Identifier of the component type to remove from the entity.
	*/
	struct RemoveData
	{
		public:
			ComponentTypeID compId;
	};

	/*! @struct SetParentData include/ECS/command.h
		@brief Payload for `CmdType::SetParent`.
		@param[in] parent The parent `Entity` to assign to the target entity.
	*/
	struct SetParentData
	{
		public:
			Entity parent;
	};

	/*! @class Command include/ECS/command.h
		@brief Value object representing a single ECS mutation command.
		@details `Command` stores a `CmdType`, the target `Entity`, and a small payload variant whose type depends on the command. Factory
	   helpers are provided to construct commands in a concise and exception-safe manner. The class is a simple POD-like value and
	   intentionally exposes no mutation API beyond the static makers.
	*/
	class Command
	{
		public:
			// MARK: Getters

			/*! @brief Returns the command type.
				@return `CmdType` indicating the kind of operation this command represents.
			*/
			ATTR_NODISCARD constexpr CmdType getType() const noexcept
			{
				return mType;
			}

			/*! @brief Returns the command's global recording sequence.
				@return Monotonic sequence assigned when the command is appended to a @ref CommandBuffer.
			*/
			ATTR_NODISCARD constexpr uint64_t getSequence() const noexcept
			{
				return mSequence;
			}

			/*! @brief Returns the target entity for the command.
				@return Reference to the `Entity` targeted by the command.
			*/
			ATTR_NODISCARD constexpr Entity &getEntity() noexcept
			{
				return mEntity;
			}

			/*! @brief Returns a const reference to the target entity for the command.
				@return Const reference to the `Entity` targeted by the command.
			*/
			ATTR_NODISCARD constexpr const Entity &getEntity() const noexcept
			{
				return mEntity;
			}

			/*! @brief Returns a const reference to the payload variant.
				@return Const reference to the internal `std::variant` holding command data.
			*/
			ATTR_NODISCARD constexpr const auto &getData() const noexcept
			{
				return mData;
			}

			/*! @brief Returns a mutable reference to the payload variant.
				@return Mutable reference to the internal `std::variant` holding command data.
			*/
			ATTR_NODISCARD constexpr auto &getData() noexcept
			{
				return mData;
			}

			// MARK: Factory Helpers

			/*! @brief Create an `AddComponent` command that stores a copy of `value`.
				@tparam T Type of the component value to add.
				@param[in] entity Target entity to add the component to.
				@param[in] value Component value to store; forwarded into `ComponentStorage`.
				@return A constructed `Command` with `CmdType::AddComponent`.
				@throws std::bad_alloc If storage allocation fails.
				@throws Any exception propagated by the component constructor.
			*/
			template <typename T>
			static Command makeAdd(const Entity &entity, T &&value)
			{
				Command cmd;
				cmd.mType = CmdType::AddComponent;
				cmd.mEntity = entity;
				cmd.mData = AddData{ComponentStorage::create<T>(std::forward<T>(value))};
				return cmd;
			}

			/*! @brief Create a `RemoveComponent` command.
				@param[in] entity Target entity to remove the component from.
				@param[in] compId Component type identifier to remove.
				@return A constructed `Command` with `CmdType::RemoveComponent`.
			*/
			static constexpr Command makeRemove(const Entity &entity, const ComponentTypeID compId) noexcept
			{
				Command cmd;
				cmd.mType = CmdType::RemoveComponent;
				cmd.mEntity = entity;
				cmd.mData = RemoveData{compId};
				return cmd;
			}

			/*! @brief Create a `Destroy` command for the given entity.
				@param[in] entity Target entity to destroy.
				@return A constructed `Command` with `CmdType::Destroy`.
			*/
			static constexpr Command makeDestroy(const Entity &entity) noexcept
			{
				Command cmd;
				cmd.mType = CmdType::Destroy;
				cmd.mEntity = entity;
				return cmd;
			}

			/*! @brief Create a `SetParent` command to assign `parent` to `child`.
				@param[in] child The child entity whose parent will be set.
				@param[in] parent The parent entity to assign.
				@return A constructed `Command` with `CmdType::SetParent`.
			*/
			static constexpr Command makeSetParent(const Entity &child, const Entity &parent) noexcept
			{
				Command cmd;
				cmd.mType = CmdType::SetParent;
				cmd.mEntity = child;
				cmd.mData = SetParentData{parent};
				return cmd;
			}

		private:
			/*! @brief Assigns the command's global recording sequence.
				@param[in] sequence Monotonic sequence generated by the owning command buffer.
			*/
			constexpr void setSequence(const uint64_t sequence) noexcept
			{
				mSequence = sequence;
			}

			/*! @var mData
				@brief Variant holding the command-specific payload.
			*/
			std::variant<AddData, RemoveData, SetParentData> mData;

			/*! @var mEntity
				@brief Target entity for this command.
			*/
			Entity mEntity{};

			/*! @var mType
				@brief Discriminator indicating which payload is active in `mData`.
			*/
			CmdType mType{};

			/*! @var mSequence
				@brief Monotonic recording order assigned by @ref CommandBuffer.
			*/
			uint64_t mSequence{};

			friend class CommandBuffer;
	};
} // namespace Dimensia::ECS

#endif