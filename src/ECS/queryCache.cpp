/*! \file queryCache.cpp
	\brief Contains the function definitions for creating a queryCache
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/queryCache.h"

#include <utility>
#include <vector>

#include "ECS/archetype.h"
#include "ECS/componentMask.h"

namespace Dimensia::ECS
{
	// MARK: Member Functions

	void QueryCache::addArchetype(const ComponentMask &regularMask, Archetype *arch)
	{
		mArchetypes.emplace_back(regularMask, arch);

		clearResults();
	}

	void QueryCache::removeArchetype(const Archetype *arch)
	{
		std::erase_if(mArchetypes, [arch](const std::pair<ComponentMask, Archetype *> &pred) noexcept { return pred.second == arch; });

		clearResults();
	}

	const std::vector<Archetype *> &QueryCache::get(const ComponentMask &requiredMask) const
	{
		auto iterator{mResults.find(requiredMask)};

		if (iterator != mResults.end())
		{
			return iterator->second;
		}

		std::vector<Archetype *> matching;
		matching.reserve(mArchetypes.size());

		for (const auto &[mask, arch] : mArchetypes)
		{
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
		mResults.clear();
	}
} // namespace Dimensia::ECS