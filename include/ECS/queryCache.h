/*! @file queryCache.h
	@brief Caches archetype lists for component-mask queries.
	@details `QueryCache` maintains a list of registered archetypes with their associated `ComponentMask` and provides a cached mapping from
   a required component mask to matching archetypes. The cache is computed lazily on first `get()` and stored in `mResults` to avoid
   repeated scanning of `mArchetypes`.
	@note Returned references from `get()` point into the internal cache and remain valid until the cache is cleared or mutated (for example
   via `addArchetype()`, `removeArchetype()` or `clearResults()`). Not thread-safe for concurrent writes; external synchronization is
   required for concurrent access.
	@date 02/14/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_QUERYCACHE_H
#define INCLUDE_ECS_QUERYCACHE_H

#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ECS/archetype.h"

#include "componentMask.h"

namespace Dimensia::ECS
{
	/*! @class QueryCache include/ECS/queryCache.h
		@brief Caches archetype lists for component-mask queries.
		@details Maintains a registry of archetypes paired with their regular masks and lazily computes and stores query results in
	   `mResults` to avoid repeated scans. Use `addArchetype()` / `removeArchetype()` to keep the cache consistent with the archetype set;
	   those calls clear cached results automatically.
		@note Returned references from `get()` are references into the internal cache and remain valid until `clearResults()` or any
	   modifying call is performed.
		@date 02/14/2026
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	class QueryCache
	{
		public:
			// MARK: Member Functions

			/*! @brief Adds an archetype to the queryable set.
				@param[in] regularMask The archetype's component mask describing stored components.
				@param[in] arch Non-owning pointer to the archetype to register.
				@post The internal cache is cleared so subsequent `get()` calls will recompute results.
			*/
			void addArchetype(const ComponentMask &regularMask, Archetype *arch);

			/*! @brief Removes an archetype from the registry.
				@param[in] arch Pointer to the archetype to remove; comparison is pointer identity.
				@post The internal cache is cleared so subsequent `get()` calls will recompute results.
			*/
			void removeArchetype(const Archetype *arch);

			/*! @brief Returns the matching archetypes for `requiredMask`.
				@details If the result for `requiredMask` is not cached, the function computes the matching archetypes by scanning
			   `mArchetypes` and stores the result in `mResults` before returning a reference to the cached vector.
				@param[in] requiredMask Component mask that must be present in matching archetypes.
				@note The returned reference is valid until the cache is cleared or mutated.
				@return A reference to a vector of matching `Archetype*` stored in the internal cache.
			*/
			const std::vector<Archetype *> &get(const ComponentMask &requiredMask) const;

			/*! @brief Clears all cached query results.
				@post Subsequent `get()` calls will recompute matches.
			*/
			void clearResults() noexcept;

		private:
			/*! @var mArchetypes
				@brief Registered archetypes paired with their regular component masks.
				@details Each element is a pair of the archetype's `ComponentMask` and a non-owning pointer to the `Archetype` instance.
			*/
			std::vector<std::pair<ComponentMask, Archetype *>> mArchetypes{};

			/*! @var mResults
				@brief Mutable cache mapping query masks to matching archetypes.
				@details Marked `mutable` so `get()` may populate the cache from const contexts. Keys are `ComponentMask` values and mapped
			   vectors are stored inside the container; references returned by `get()` point into these vectors.
			*/
			mutable std::unordered_map<ComponentMask, std::vector<Archetype *>> mResults{};

			/*! @var mMutex
				@brief Shared mutex protecting `mResults` for concurrent reads and exclusive writes.
			*/
			mutable std::shared_mutex mMutex{};
	};
} // namespace Dimensia::ECS

#endif