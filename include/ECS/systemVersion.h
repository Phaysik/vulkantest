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
#include <cassert>

#include "Core/attributeMacros.h"
#include "ECS/processChunkHelpers.h"

#include "chunkVersion.h"
#include "componentMask.h"
#include "componentRegistry.h"

namespace Dimensia::ECS
{
	using Dimensia::Registry::MAX_COMPONENTS;
	using Dimensia::Registry::VersionType;

	class SystemVersion
	{
		public:
			// MARK: Constructor

			explicit constexpr SystemVersion()
			{
				mComponentVersions.fill(0);
			}

			// MARK: Getters

			ATTR_NODISCARD constexpr VersionType getVersion() const noexcept
			{
				return mVersion;
			}

			ATTR_DEPRECATED ATTR_NODISCARD constexpr std::array<VersionType, MAX_COMPONENTS> getComponentVersions() const noexcept
			{
				return mComponentVersions;
			}

			ATTR_NODISCARD constexpr VersionType getComponentVersion(const ComponentTypeID componentTypeID) const noexcept
			{
				assert(componentTypeID < MAX_COMPONENTS);

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				return mComponentVersions[componentTypeID];
			}

			// MARK: Setters

			constexpr void setVersion(const VersionType version) noexcept
			{
				mVersion = version;
			}

			constexpr void setComponentVersion(const ComponentTypeID componentTypeID, const VersionType version) noexcept
			{
				assert(componentTypeID < MAX_COMPONENTS);

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				mComponentVersions[componentTypeID] = version;
			}

			// MARK: Member Functions

			ATTR_NODISCARD constexpr bool needsUpdate(const ChunkVersion &chunk, const ComponentMask &requiredComponents) const
			{
				if (chunk.getVersion() > mVersion)
				{
					return true;
				}

				bool needs{false};

				forEachSetBit(requiredComponents, [&](const ComponentTypeID componentTypeID) {
					assert(componentTypeID < MAX_COMPONENTS);

					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					if (chunk.getComponentVersion(componentTypeID) > mComponentVersions[componentTypeID])
					{
						needs = true;
					}
				});

				return needs;
			}

			constexpr void update(const ChunkVersion &chunk) noexcept
			{
				// mComponentVersions intentionally not updated here – they are updated per‑chunk after processing
				mVersion = chunk.getVersion();
			}

		private:
			std::array<VersionType, Registry::MAX_COMPONENTS> mComponentVersions{};
			VersionType mVersion{0};
	};
} // namespace Dimensia::ECS

#endif