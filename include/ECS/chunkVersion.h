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
#include <cassert>
#include <compare>

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

			explicit constexpr ChunkVersion()
			{
				mComponentVersions.fill(0);
			}

			// MARK: Comparison

			std::strong_ordering operator<=>(const ChunkVersion &) const noexcept = default;

			// MARK: Getters

			ATTR_NODISCARD constexpr VersionType getVersion() const noexcept
			{
				return mVersion;
			}

			ATTR_DEPRECATED ATTR_NODISCARD constexpr const std::array<VersionType, MAX_COMPONENTS> &getComponentVersions() const noexcept
			{
				return mComponentVersions;
			}

			ATTR_NODISCARD constexpr VersionType getComponentVersion(const ComponentTypeID componentTypeID) const noexcept
			{
				assert(componentTypeID < MAX_COMPONENTS);

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				return mComponentVersions[componentTypeID];
			}

			// MARK: Member Functions

			constexpr void bump() noexcept
			{
				++mVersion;
			}

			constexpr void bumpComponent(const ComponentTypeID componentTypeID) noexcept
			{
				assert(componentTypeID < MAX_COMPONENTS);

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				++mComponentVersions[componentTypeID];
			}

			constexpr void reset()
			{
				mVersion = 1;
				mComponentVersions.fill(1);
			}

		private:
			std::array<VersionType, MAX_COMPONENTS> mComponentVersions{};
			VersionType mVersion{1};
	};
} // namespace Dimensia::ECS

#endif