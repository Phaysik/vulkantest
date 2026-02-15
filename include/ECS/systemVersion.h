/*! \file systemVersion.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_SYSTEMVERSION_H
#define INCLUDE_ECS_SYSTEMVERSION_H

#include <array>

#include "chunkVersion.h"
#include "componentMask.h"
#include "componentRegistry.h"

namespace Dimensia::ECS
{
	using Registry::VersionType;

	struct SystemVersion
	{
			VersionType version;
			std::array<VersionType, Registry::MAX_COMPONENTS> componentVersions;

			SystemVersion();
			bool needsUpdate(const ChunkVersion &chunk, ComponentMask requiredComponents) const;
			void update(const ChunkVersion &chunk);
	};
} // namespace Dimensia::ECS

#endif