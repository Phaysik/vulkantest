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

#include "ECS/archetype.h"

#include "componentMask.h"

namespace Dimensia::ECS
{
	class QueryCache
	{
		public:
			// MARK: Member Functions

			void addArchetype(const ComponentMask &regularMask, Archetype *arch);

			void removeArchetype(const Archetype *arch);

			const std::vector<Archetype *> &get(const ComponentMask &requiredMask) const;

			void clearResults() noexcept;

		private:
			std::vector<std::pair<ComponentMask, Archetype *>> mArchetypes;
			mutable std::unordered_map<ComponentMask, std::vector<Archetype *>> mResults;
	};
} // namespace Dimensia::ECS

#endif