/*! \file command.h
	\brief Contains the function declarations for creating a Command
	\date 02/17/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMMAND_H
#define INCLUDE_ECS_COMMAND_H

#include <array>
#include <variant>

#include "Core/attributeMacros.h"
#include "Core/typedefs.h"

#include "componentRegistry.h"
#include "entity.h"

namespace Dimensia::ECS
{
	using Dimensia::Registry::componentId;

	using Dimensia::Registry::ComponentTypeID;

	enum class CmdType : Dimensia::Core::ub
	{
		AddComponent,
		RemoveComponent,
		Destroy,
		SetParent
	};

	struct AddData
	{
		public:
			ComponentTypeID compId;
			alignas(Dimensia::Registry::MAX_COMPONENT_ALIGN) std::array<std::byte, Dimensia::Registry::MAX_COMPONENT_ALIGN> buffer;
	};

	struct RemoveData
	{
		public:
			ComponentTypeID compId;
	};

	struct SetParentData
	{
		public:
			Entity parent;
	};

	class Command
	{
		public:
			// MARK: Getters

			ATTR_NODISCARD constexpr CmdType getType() const noexcept
			{
				return mType;
			}

			ATTR_NODISCARD constexpr const Entity &getEntity() const noexcept
			{
				return mEntity;
			}

			ATTR_NODISCARD constexpr const auto &getData() const noexcept
			{
				return mData;
			}

			ATTR_NODISCARD constexpr auto &getData() noexcept
			{
				return mData;
			}

			// MARK: Template Member Functions

			template <typename T>
			static Command makeAdd(const Entity &entity, T &&value) noexcept
			{
				Command cmd;
				cmd.mType = CmdType::AddComponent;
				cmd.mEntity = entity;

				AddData addData{};
				addData.compId = componentId<T>();
				new (addData.buffer.data()) T(std::forward<T>(value));
				cmd.mData = addData;
				return cmd;
			}

			static constexpr Command makeRemove(const Entity &entity, const ComponentTypeID compId) noexcept
			{
				Command cmd;
				cmd.mType = CmdType::RemoveComponent;
				cmd.mEntity = entity;
				cmd.mData = RemoveData{compId};
				return cmd;
			}

			static constexpr Command makeDestroy(const Entity &entity) noexcept
			{
				Command cmd;
				cmd.mType = CmdType::Destroy;
				cmd.mEntity = entity;
				return cmd;
			}

			static constexpr Command makeSetParent(const Entity &child, const Entity &parent) noexcept
			{
				Command cmd;
				cmd.mType = CmdType::SetParent;
				cmd.mEntity = child;
				cmd.mData = SetParentData{parent};
				return cmd;
			}

		private:
			std::variant<AddData, RemoveData, SetParentData> mData;
			Entity mEntity{};
			CmdType mType{};
	};
} // namespace Dimensia::ECS

#endif