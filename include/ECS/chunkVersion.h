/*! @file chunkVersion.h
	@brief Tracks per-chunk and per-component version counters used for change detection.
	@details `ChunkVersion` stores a global version number for a chunk and an array of per-component versions. Systems and caches use these
   values to determine whether a chunk or specific component data have changed and therefore require reprocessing. This header documents the
   version bumping and reset semantics used by the ECS.
	@date 02/14/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
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

	/*! @class ChunkVersion include/ECS/chunkVersion.h
		@brief Represents version metadata for an archetype chunk.
		@details Contains a global chunk `mVersion` and an array `mComponentVersions` indexed by `ComponentTypeID`. Callers should use
	   `bump()` to indicate a global change and `bumpComponent()` to indicate a component-level change. `reset()` initializes the version
	   counters to 1 to avoid zero-valued versions.
		@note Thread-safety depends on external synchronization when mutating versions.
	*/
	class ChunkVersion
	{
		public:
			// MARK: Constructor

			/*! @brief Default-constructs a ChunkVersion.
				@details Initializes the per-component versions to zero.
			*/
			explicit constexpr ChunkVersion()
			{
				mComponentVersions.fill(0);
			}

			// MARK: Comparison

			/*! @brief Defaulted three-way comparison for `ChunkVersion`.
				@return A `std::strong_ordering` comparing component versions and the global version.
			*/
			std::strong_ordering operator<=>(const ChunkVersion &) const noexcept = default;

			// MARK: Getters

			/*! @brief Returns the global chunk version.
				@return The stored `VersionType` for the chunk.
			*/
			ATTR_NODISCARD constexpr VersionType getVersion() const noexcept
			{
				return mVersion;
			}

			/*! @brief Returns a const reference to the per-component versions.
				@deprecated Prefer `getComponentVersion()` for single-component queries.
				@return Const reference to the internal per-component versions array.
			*/
			ATTR_DEPRECATED ATTR_NODISCARD constexpr const std::array<VersionType, MAX_COMPONENTS> &getComponentVersions() const noexcept
			{
				return mComponentVersions;
			}

			/*! @brief Returns the stored version for a specific component type.
				@param[in] componentTypeID Index of the component type (must be < @ref MAX_COMPONENTS).
				@pre `componentTypeID < MAX_COMPONENTS`
				@return The `VersionType` for the given component.
			*/
			ATTR_NODISCARD constexpr VersionType getComponentVersion(const ComponentTypeID componentTypeID) const noexcept
			{
				assert(componentTypeID < MAX_COMPONENTS);

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				return mComponentVersions[componentTypeID];
			}

			// MARK: Member Functions

			/*! @brief Increment the global chunk version.
				@post `mVersion` is incremented by one.
			*/
			constexpr void bump() noexcept
			{
				++mVersion;
			}

			/*! @brief Increment the version counter for a specific component type.
				@param[in] componentTypeID The component type index to bump (must be < @ref MAX_COMPONENTS).
				@post The per-component version for `componentTypeID` is incremented by one.
			*/
			constexpr void bumpComponent(const ComponentTypeID componentTypeID) noexcept
			{
				assert(componentTypeID < MAX_COMPONENTS);

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				++mComponentVersions[componentTypeID];
			}

			/*! @brief Reset all version counters to 1.
				@details Initializes the global version and all per-component versions to 1. Using 1 avoids confusion with
			   default-initialized zero values.
			*/
			constexpr void reset()
			{
				mVersion = 1;
				mComponentVersions.fill(1);
			}

		private:
			/*! @var mComponentVersions
				@brief Per-component version counters indexed by `ComponentTypeID`.
				@details Used to detect component-level changes inside a chunk.
			*/
			std::array<VersionType, MAX_COMPONENTS> mComponentVersions{};

			/*! @var mVersion
				@brief Global version counter for the chunk.
				@details Incremented by `bump()` to indicate any change affecting the whole chunk.
			*/
			VersionType mVersion{1};
	};
} // namespace Dimensia::ECS

#endif