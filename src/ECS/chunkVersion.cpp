/*! \file chunkVersion.cpp
	\brief Contains the function definitions for creating a chunkVersion
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/chunkVersion.h"

#include "Core/attributeMacros.h"
#include "ECS/componentRegistry.h"

namespace Dimensia::ECS
{
	using Dimensia::Registry::ComponentTypeId;
	using Dimensia::Registry::VersionType;

	// MARK: Constructor

	ChunkVersion::ChunkVersion()
	{
		mComponentVersions.fill(1);
	}

	// MARK: Getters

	ATTR_NODISCARD VersionType ChunkVersion::getVersion() const noexcept
	{
		return mVersion;
	}

	ATTR_DEPRECATED ATTR_NODISCARD const std::array<VersionType, Dimensia::Registry::MAX_COMPONENTS> &ChunkVersion::getComponentVersions()
		const noexcept
	{
		return mComponentVersions;
	}

	ATTR_NODISCARD VersionType ChunkVersion::getComponentVersion(const ComponentTypeId componentTypeID) const
	{
		return mComponentVersions.at(componentTypeID);
	}

	// Mark: Member Functions

	void ChunkVersion::bump() noexcept
	{
		++mVersion;
	}

	void ChunkVersion::bumpComponent(const ComponentTypeId componentTypeID)
	{
		++mComponentVersions.at(componentTypeID);
	}
} // namespace Dimensia::ECS