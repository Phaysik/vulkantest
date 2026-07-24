/*! @file chunkVersion.h
	@brief Tracks per-chunk and per-component version counters used for change detection.
	@details `ChunkVersion` stores world-global structural and per-component epoch stamps. Systems and caches use these
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
#include <atomic>
#include <cassert>

#include "Core/attributeMacros.h"
#include "ECS/componentRegistry.h"

namespace Dimensia::ECS
{
	using Dimensia::Registry::ComponentTypeID;
	using Dimensia::Registry::MAX_COMPONENTS;
	using Dimensia::Registry::VersionType;

	/*! @class ChunkVersion include/ECS/chunkVersion.h
		@brief Represents version metadata for an archetype chunk.
		@details Contains a structural chunk epoch and component epochs indexed by `ComponentTypeID`. Callers stamp versions allocated by
	   the owning ECS world; values are never incremented independently by a chunk.
		@note Thread-safety depends on external synchronization when mutating versions.
	*/
	class ChunkVersion
	{
		public:
			// MARK: Constructor

			explicit ChunkVersion() noexcept
			{
				for (auto &version : mComponentVersions)
				{
					version.store(0, std::memory_order_relaxed);
				}
			}

			ChunkVersion(const ChunkVersion &other) noexcept : ChunkVersion()
			{
				*this = other;
			}

			ChunkVersion &operator=(const ChunkVersion &other) noexcept
			{
				if (this != &other)
				{
					mVersion.store(other.getVersion(), std::memory_order_relaxed);
					for (ComponentTypeID componentTypeID{0}; componentTypeID < MAX_COMPONENTS; ++componentTypeID)
					{
						mComponentVersions.at(componentTypeID).store(other.getComponentVersion(componentTypeID), std::memory_order_relaxed);
					}
				}
				return *this;
			}

			ChunkVersion(ChunkVersion &&other) noexcept : ChunkVersion()
			{
				assignFrom(other);
			}

			ChunkVersion &operator=(ChunkVersion &&other) noexcept
			{
				if (this != &other)
				{
					assignFrom(other);
				}
				return *this;
			}

			~ChunkVersion() = default;

			// MARK: Getters

			/*! @brief Returns the global chunk version.
				@return The stored `VersionType` for the chunk.
			*/
			ATTR_NODISCARD VersionType getVersion() const noexcept
			{
				return mVersion.load(std::memory_order_acquire);
			}

			/*! @brief Returns the stored version for a specific component type.
				@param[in] componentTypeID Index of the component type (must be < @ref MAX_COMPONENTS).
				@pre `componentTypeID < MAX_COMPONENTS`
				@return The `VersionType` for the given component.
			*/
			ATTR_NODISCARD VersionType getComponentVersion(const ComponentTypeID componentTypeID) const noexcept
			{
				assert(componentTypeID < MAX_COMPONENTS);

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				return mComponentVersions[componentTypeID].load(std::memory_order_acquire);
			}

			// MARK: Member Functions

			/*! @brief Stamps a structural change from the world-global timeline. */
			void markStructural(const VersionType version) noexcept
			{
				storeMaximum(mVersion, version);
			}

			/*! @brief Increment the version counter for a specific component type.
				@param[in] componentTypeID The component type index to bump (must be < @ref MAX_COMPONENTS).
				@post The per-component version for `componentTypeID` is incremented by one.
			*/
			void markComponent(const ComponentTypeID componentTypeID, const VersionType version) noexcept
			{
				assert(componentTypeID < MAX_COMPONENTS);

				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				storeMaximum(mComponentVersions[componentTypeID], version);
			}

			/*! @brief Reset all version counters to 1.
				@details Initializes the global version and all per-component versions to 1. Using 1 avoids confusion with
			   default-initialized zero values.
			*/
			void reset() noexcept
			{
				mVersion.store(1, std::memory_order_relaxed);
				for (auto &version : mComponentVersions)
				{
					version.store(1, std::memory_order_relaxed);
				}
			}

		private:
			void assignFrom(const ChunkVersion &other) noexcept
			{
				mVersion.store(other.getVersion(), std::memory_order_relaxed);
				for (ComponentTypeID componentTypeID{0}; componentTypeID < MAX_COMPONENTS; ++componentTypeID)
				{
					mComponentVersions.at(componentTypeID).store(other.getComponentVersion(componentTypeID), std::memory_order_relaxed);
				}
			}

			static void storeMaximum(std::atomic<VersionType> &target, const VersionType version) noexcept
			{
				VersionType current{target.load(std::memory_order_relaxed)};
				while (current < version
					   && !target.compare_exchange_weak(current, version, std::memory_order_release, std::memory_order_relaxed))
				{
				}
			}

			/*! @var mComponentVersions
				@brief Per-component version counters indexed by `ComponentTypeID`.
				@details Used to detect component-level changes inside a chunk.
			*/
			std::array<std::atomic<VersionType>, MAX_COMPONENTS> mComponentVersions{};

			/*! @var mVersion
				@brief Global version counter for the chunk.
				@details Incremented by `bump()` to indicate any change affecting the whole chunk.
			*/
			std::atomic<VersionType> mVersion{1};
	};
} // namespace Dimensia::ECS

#endif