/*! \file queryCache.cpp
	\brief Contains the function definitions for creating a queryCache
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/queryCache.h"

#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

#include "ECS/archetype.h"
#include "ECS/componentMask.h"

namespace Dimensia::ECS
{
	// MARK: Member Functions

	void QueryCache::addArchetype(const ComponentMask &regularMask, Archetype *arch)
	{
		const std::unique_lock lock(mMutex);
		mArchetypes.emplace_back(regularMask, arch);

		// Incrementally update existing cached results: add the new archetype
		// to any cached query whose required mask is satisfied by the new archetype.
		for (auto &[queryMask, results] : mResults)
		{
			// NOLINTNEXTLINE(readability-redundant-parentheses)
			if ((regularMask & queryMask) == queryMask)
			{
				results.push_back(arch);
			}
		}
	}

	void QueryCache::removeArchetype(const Archetype *arch)
	{
		const std::unique_lock lock(mMutex);
		std::erase_if(mArchetypes, [arch](const std::pair<ComponentMask, Archetype *> &pred) noexcept { return pred.second == arch; });
		mResults.clear();
	}

	const std::vector<Archetype *> &QueryCache::get(const ComponentMask &requiredMask) const
	{
		// Fast path: check under shared lock
		{
			const std::shared_lock readLock(mMutex);
			auto iterator{mResults.find(requiredMask)};

			if (iterator != mResults.end())
			{
				return iterator->second;
			}
		}

		// Slow path: compute and insert under exclusive lock
		const std::unique_lock writeLock(mMutex);

		// Double-check after acquiring exclusive lock
		auto iterator{mResults.find(requiredMask)};

		if (iterator != mResults.end())
		{
			return iterator->second;
		}

		std::vector<Archetype *> matching;
		matching.reserve(mArchetypes.size());

		for (const auto &[mask, arch] : mArchetypes)
		{
			// NOLINTNEXTLINE(readability-redundant-parentheses)
			if ((mask & requiredMask) == requiredMask)
			{
				matching.push_back(arch);
			}
		}

		auto emplaced{mResults.emplace(requiredMask, std::move(matching))};
		return emplaced.first->second;
	}

	void QueryCache::clearResults() noexcept
	{
		const std::unique_lock lock(mMutex);
		mResults.clear();
	}
} // namespace Dimensia::ECS