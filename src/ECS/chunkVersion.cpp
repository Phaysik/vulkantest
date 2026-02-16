/*! \file chunkVersion.cpp
	\brief Contains the function definitions for creating a chunkVersion
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/chunkVersion.h"

#include <cassert>

#include "Core/attributeMacros.h"
#include "ECS/componentRegistry.h"

namespace Dimensia::ECS
{
	using Dimensia::Registry::ComponentTypeID;
	using Dimensia::Registry::MAX_COMPONENTS;
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

	ATTR_DEPRECATED ATTR_NODISCARD const std::array<VersionType, MAX_COMPONENTS> &ChunkVersion::getComponentVersions() const noexcept
	{
		return mComponentVersions;
	}

	ATTR_NODISCARD VersionType ChunkVersion::getComponentVersion(const ComponentTypeID componentTypeID) const noexcept
	{
		assert(componentTypeID < MAX_COMPONENTS);

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		return mComponentVersions[componentTypeID];
	}

	// Mark: Member Functions

	void ChunkVersion::bump() noexcept
	{
		++mVersion;
	}

	void ChunkVersion::bumpComponent(const ComponentTypeID componentTypeID) noexcept
	{
		assert(componentTypeID < MAX_COMPONENTS);

		// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		++mComponentVersions[componentTypeID];
	}
} // namespace Dimensia::ECS