/*! \file transformComponent.cpp
	\brief Contains the function definitions for creating a transform component
	\date 02/12/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#include "Components/Transform/transformComponent.h"

namespace Dimensia::Components
{
	void TransformComponent::setPosition(const glm::vec3 &position)
	{
		mPosition = position;
		mMatrixDirty = true;
	}

	void TransformComponent::setRotation(const glm::quat &rotation)
	{
		mRotation = rotation;
		mMatrixDirty = true;
	}

	void TransformComponent::setScale(const glm::vec3 &scale)
	{
		mScale = scale;
		mMatrixDirty = true;
	}

	ATTR_NODISCARD glm::mat4 TransformComponent::getModelMatrix()
	{
		if (mMatrixDirty)
		{
			updateModelMatrix();
		}

		return mModelMatrix;
	}

	void TransformComponent::updateModelMatrix()
	{
		const glm::mat4 translationMatrix{glm::translate(glm::mat4(1.0F), mPosition)};
		const glm::quat rotationX{glm::angleAxis(mRotation.x, glm::vec3(1.0F, 0.0F, 0.0F))};
		const glm::quat rotationY{glm::angleAxis(mRotation.y, glm::vec3(0.0F, 1.0F, 0.0F))};
		const glm::quat rotationZ{glm::angleAxis(mRotation.z, glm::vec3(0.0F, 0.0F, 1.0F))};

		const glm::quat combinedRotation{rotationZ * rotationY * rotationX}; // ZYX order
		const glm::mat4 rotationMatrix{glm::mat4_cast(combinedRotation)};
		const glm::mat4 scaleMatrix{glm::scale(glm::mat4(1.0F), mScale)};

		mModelMatrix = translationMatrix * rotationMatrix * scaleMatrix;
		mMatrixDirty = false;
	}
} // namespace Dimensia::Components