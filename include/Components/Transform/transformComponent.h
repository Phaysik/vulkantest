/*! \file transformComponent.h
	\brief Contains the function declarations for creating a transform component
	\date 02/12/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#ifndef INCLUDE_COMPONENTS_TRANSFORM_TRANSFORMCOMPONENT_H
#define INCLUDE_COMPONENTS_TRANSFORM_TRANSFORMCOMPONENT_H

#include "Core/attributeMacros.h"
#include "ECS/component.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Dimensia::Components
{
	class TransformComponent : public Dimensia::ECS::Component
	{
		public:
			explicit TransformComponent() : Dimensia::ECS::Component("TransformComponent") {}

			TransformComponent(const TransformComponent &other) = default;
			TransformComponent(TransformComponent &&other) noexcept = default;

			TransformComponent &operator=(const TransformComponent &other) = default;
			TransformComponent &operator=(TransformComponent &&other) noexcept = default;

			~TransformComponent() override = default;

			void setPosition(const glm::vec3 &position);

			void setRotation(const glm::quat &rotation);

			void setScale(const glm::vec3 &scale);

			ATTR_NODISCARD constexpr const glm::vec3 &getPosition() const
			{
				return mPosition;
			}

			ATTR_NODISCARD constexpr const glm::quat &getRotation() const
			{
				return mRotation;
			}

			ATTR_NODISCARD constexpr const glm::vec3 &getScale() const
			{
				return mScale;
			}

			ATTR_NODISCARD glm::mat4 getModelMatrix();

		private:
			void updateModelMatrix();

		private:
			glm::vec3 mPosition{glm::vec3(0.0F)};
			glm::quat mRotation{glm::quat(1.0F, 0.0F, 0.0F, 0.0F)}; // Identity quaternion
			glm::vec3 mScale{glm::vec3(1.0F)};

			glm::mat4 mModelMatrix{glm::mat4(1.0F)};
			bool mMatrixDirty{true};
	};
} // namespace Dimensia::Components

#endif