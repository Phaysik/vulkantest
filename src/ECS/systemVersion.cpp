/*! \file systemVersion.cpp
	\brief Contains the function definitions for creating a systemVersion
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/systemVersion.h"

#include "ECS/processChunkHelpers.h"

namespace Dimensia::ECS
{

	SystemVersion::SystemVersion() : version(0)
	{
		componentVersions.fill(0);
	}

	bool SystemVersion::needsUpdate(const ChunkVersion &chunk, ComponentMask requiredComponents) const
	{
		if (chunk.version > version)
		{
			return true;
		}
		bool needs = false;
		forEachSetBit(requiredComponents, [&](ComponentTypeId id) {
			if (chunk.componentVersions[id] > componentVersions[id])
			{
				needs = true;
			}
		});
		return needs;
	}

	void SystemVersion::update(const ChunkVersion &chunk)
	{
		version = chunk.version;
		// componentVersions intentionally not updated here – they are updated per‑chunk after processing
	}
} // namespace Dimensia::ECS