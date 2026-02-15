/*! \file chunkVersion.cpp
	\brief Contains the function definitions for creating a chunkVersion
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/chunkVersion.h"

namespace Dimensia::ECS
{
	ChunkVersion::ChunkVersion() : version(1)
	{
		componentVersions.fill(1);
	}

	void ChunkVersion::bump()
	{
		++version;
	}

	void ChunkVersion::bumpComponent(Registry::ComponentTypeId id)
	{
		++componentVersions[id];
	}
} // namespace Dimensia::ECS