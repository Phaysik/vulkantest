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
	using Registry::VersionType;

	// MARK: Constructor

	ChunkVersion::ChunkVersion()
	{
		mComponentVersions.fill(1);
	}

	// MARK: Getters

	ATTR_NODISCARD VersionType ChunkVersion::getVersion() const
	{
		return mVersion;
	}

	ATTR_NODISCARD const std::array<VersionType, Registry::MAX_COMPONENTS> &ChunkVersion::getComponentVersions() const
	{
		return mComponentVersions;
	}

	// Mark: Member Functions

	void ChunkVersion::bump()
	{
		++mVersion;
	}

	void ChunkVersion::bumpComponent(const Registry::ComponentTypeId componentTypeID)
	{
		++mComponentVersions.at(componentTypeID);
	}
} // namespace Dimensia::ECS