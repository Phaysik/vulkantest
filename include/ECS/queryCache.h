/*! \file queryCache.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_QUERYCACHE_H
#define INCLUDE_ECS_QUERYCACHE_H

#include <unordered_map>
#include <utility>
#include <vector>

#include "componentMask.h"

// Forward declaration
class Archetype;

class QueryCache
{
	public:
		void addArchetype(ComponentMask regularMask, Archetype *arch);
		void removeArchetype(Archetype *arch);
		const std::vector<Archetype *> &get(ComponentMask requiredMask) const;
		void clear();

	private:
		std::vector<std::pair<ComponentMask, Archetype *>> archetypes_;
		mutable std::unordered_map<ComponentMask, std::vector<Archetype *>> results_;
};

#endif