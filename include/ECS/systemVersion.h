/*! @file systemVersion.h
	@brief Tracks version state used by systems to determine chunk processing.
	@details Provides `SystemVersion` which stores a system-wide version and per-component versions used to detect whether a `ChunkVersion`
   requires reprocessing by the system. See the `SystemVersion` member documentation for usage notes and thread-safety remarks.
	@date 02/14/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
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

	/*! @class SystemVersion include/ECS/systemVersion.h
		@brief Tracks the processed version state for a system and its components.
		@details Stores a global system `mVersion` and a per-component array `mComponentVersions` used to determine whether a given
	   `ChunkVersion` requires processing by the system. The per-component versions are updated externally on a per-chunk basis after
	   processing; calling `update()` updates only the system version (see @ref update()).
		@pre ComponentTypeID values passed to accessors must be less than @ref MAX_COMPONENTS.
		@note Not thread-safe for concurrent writes. Thread-safety for reads depends on external synchronization of version updates.
		@date 02/14/2026
		@version x.x.x
		@author Matthew Moore
	*/
	class SystemVersion
	{
		public:
			// MARK: Constructor

			/*! @brief Default-constructs a SystemVersion.
				@details Initializes all per-component versions to zero and leaves the system
				version initialized to zero via in-class member initializer.
			*/
			explicit constexpr SystemVersion()
			{
				mComponentVersions.fill(0);
			}

			// MARK: Getters

			/*! @brief Returns the system version.
				@return The current system `VersionType`.
			*/
			ATTR_NODISCARD constexpr VersionType getVersion() const noexcept
			{
				return mVersion;
			}

			/*! @brief Returns a copy of the per-component versions.
				@deprecated Returns a copy of the internal array; prefer `getComponentVersion()` for single-component queries.
				@return A copy of the internal `std::array` of component versions.
			*/
			ATTR_DEPRECATED ATTR_NODISCARD constexpr std::array<VersionType, MAX_COMPONENTS> getComponentVersions() const noexcept
			{
				return mComponentVersions;
			}

			/*! @brief Returns the stored version for a specific component type.
				@param[in] componentTypeID Identifier of the component type (must be < @ref MAX_COMPONENTS).
				@pre `componentTypeID < MAX_COMPONENTS`
				@return The stored `VersionType` for the given component type.
			*/
			ATTR_NODISCARD constexpr VersionType getComponentVersion(const ComponentTypeID componentTypeID) const noexcept
			{
				assert(componentTypeID < MAX_COMPONENTS);

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				return mComponentVersions[componentTypeID];
			}

			// MARK: Setters

			/*! @brief Sets the system version.
				@param[in] version The new system version to store.
			*/
			constexpr void setVersion(const VersionType version) noexcept
			{
				mVersion = version;
			}

			/*! @brief Sets the stored version for a specific component type.
				@param[in] componentTypeID Identifier of the component type (must be < @ref MAX_COMPONENTS).
				@param[in] version The version value to store for the component.
				@pre `componentTypeID < MAX_COMPONENTS`
			*/
			constexpr void setComponentVersion(const ComponentTypeID componentTypeID, const VersionType version) noexcept
			{
				assert(componentTypeID < MAX_COMPONENTS);

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				mComponentVersions[componentTypeID] = version;
			}

			// MARK: Member Functions

			/*! @brief Determines whether the given `chunk` requires processing by this system.
				@details Returns `true` if either the chunk's global version is newer than the stored system version, or if any of the
			   `requiredComponents` has a component version in the chunk that is newer than the stored per-component version.
				@param[in] chunk The `ChunkVersion` to compare against the stored versions.
				@param[in] requiredComponents A `ComponentMask` specifying which component versions to check.
				@note The callable `forEachSetBit` is used to iterate `requiredComponents`.
				@return `true` if processing is required; otherwise `false`.
			*/
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

			/*! @brief Update the stored system version from a processed chunk.
				@details This function updates only `mVersion` to `chunk.getVersion()`. The per-component `mComponentVersions` are
			   intentionally not updated here; they are updated separately after processing each chunk.
				@param[in] chunk The `ChunkVersion` whose version will be stored.
			*/
			constexpr void update(const ChunkVersion &chunk) noexcept
			{
				mVersion = chunk.getVersion();
			}

		private:
			/*! @var mComponentVersions
				@brief Per-component processed versions tracked by the system.
				@details Index by `ComponentTypeID` (0..MAX_COMPONENTS-1). Values represent the last seen version for each component type
			   and are used to detect component-level changes in a `ChunkVersion`.
			*/
			std::array<VersionType, Registry::MAX_COMPONENTS> mComponentVersions{};

			/*! @var mVersion
				@brief The last seen global version for this system.
				@details Compared against `ChunkVersion::getVersion()` to detect global chunk changes.
			*/
			VersionType mVersion{0};
	};
} // namespace Dimensia::ECS

#endif