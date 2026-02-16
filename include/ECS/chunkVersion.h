/*! \file chunkVersion.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_CHUNKVERSION_H
#define INCLUDE_ECS_CHUNKVERSION_H

#include <array>

#include "Core/attributeMacros.h"
#include "ECS/componentRegistry.h"

namespace Dimensia::ECS
{
	using Registry::VersionType;

	class ChunkVersion
	{
		public:
			// MARK: Constructor

			explicit ChunkVersion();

			// MARK: Getters

			ATTR_NODISCARD VersionType getVersion() const;

			ATTR_NODISCARD const std::array<VersionType, Registry::MAX_COMPONENTS> &getComponentVersions() const;

			// MARK: Member Functions

			void bump();
			void bumpComponent(const Registry::ComponentTypeId componentTypeID);

		private:
			VersionType mVersion{1};
			std::array<VersionType, Registry::MAX_COMPONENTS> mComponentVersions{};
	};
} // namespace Dimensia::ECS

#endif