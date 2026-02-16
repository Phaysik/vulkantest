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
	using Dimensia::Registry::ComponentTypeID;
	using Dimensia::Registry::MAX_COMPONENTS;
	using Dimensia::Registry::VersionType;

	class ChunkVersion
	{
		public:
			// MARK: Constructor

			explicit ChunkVersion();

			// MARK: Getters

			ATTR_NODISCARD VersionType getVersion() const noexcept;

			ATTR_DEPRECATED ATTR_NODISCARD const std::array<VersionType, MAX_COMPONENTS> &getComponentVersions() const noexcept;

			ATTR_NODISCARD VersionType getComponentVersion(const ComponentTypeID componentTypeID) const noexcept;

			// MARK: Member Functions

			void bump() noexcept;
			void bumpComponent(const ComponentTypeID componentTypeID) noexcept;

		private:
			VersionType mVersion{1};
			std::array<VersionType, MAX_COMPONENTS> mComponentVersions{};
	};
} // namespace Dimensia::ECS

#endif