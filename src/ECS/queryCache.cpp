/*! \file queryCache.cpp
	\brief Contains the function definitions for creating a queryCache
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/queryCache.h"

#include <algorithm>

void QueryCache::addArchetype(ComponentMask regularMask, Archetype *arch)
{
	archetypes_.emplace_back(regularMask, arch);
	results_.clear();
}

void QueryCache::removeArchetype(Archetype *arch)
{
	auto it = std::remove_if(archetypes_.begin(), archetypes_.end(), [arch](const auto &p) { return p.second == arch; });
	archetypes_.erase(it, archetypes_.end());
	results_.clear();
}

const std::vector<Archetype *> &QueryCache::get(ComponentMask requiredMask) const
{
	auto it = results_.find(requiredMask);
	if (it != results_.end())
	{
		return it->second;
	}

	std::vector<Archetype *> matching;
	for (auto &[mask, arch] : archetypes_)
	{
		if ((mask & requiredMask) == requiredMask)
		{
			matching.push_back(arch);
		}
	}
	auto emplaced = results_.emplace(requiredMask, std::move(matching));
	return emplaced.first->second;
}

void QueryCache::clear()
{
	results_.clear();
}