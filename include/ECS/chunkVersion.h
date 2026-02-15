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
#include <cstdint>

#include "ECS/componentRegistry.h" // for MAX_COMPONENTS

struct ChunkVersion
{
		VersionType version;
		std::array<VersionType, MAX_COMPONENTS> componentVersions;

		ChunkVersion();
		void bump();
		void bumpComponent(ComponentTypeId id);
};

#endif