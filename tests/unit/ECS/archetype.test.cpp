#include "ECS/archetype.h"

#include "Components/Position/positionComponent.h"
#include "Components/Velocity/velocityComponent.h"
#include "ECS/componentMask.h"
#include "ECS/componentRegistry.h"
#include "ECS/entity.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <stdexcept>
#include <utility>

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("Archetype empty chunk reuse")
{
	using Dimensia::Components::Position;
	using Dimensia::ECS::Archetype;
	using Dimensia::ECS::ComponentMask;
	using Dimensia::ECS::Entity;
	using Dimensia::Registry::componentID;
	using Dimensia::Registry::MAX_COMPONENTS;

	GIVEN("an emptied first chunk followed by a partially occupied second chunk")
	{
		ComponentMask mask{0};
		mask.setBit(componentID<Position>());
		Archetype archetype{mask, 1};
		Position position{.x = 1.0F, .y = 2.0F, .z = 3.0F};
		std::array<const void *, MAX_COMPONENTS> copyData{};
		std::array<void *, MAX_COMPONENTS> moveData{};
		copyData.at(componentID<Position>()) = &position;

		std::size_t firstChunkCapacity{};
		std::size_t nextEntityIndex{};
		for (;; ++nextEntityIndex)
		{
			auto location{archetype.addEntity(Entity{.index = static_cast<unsigned int>(nextEntityIndex), .generation = 1}, copyData, moveData)};
			if (location.first == 1)
			{
				break;
			}
			++firstChunkCapacity;
		}

		REQUIRE((firstChunkCapacity > 0));
		REQUIRE((archetype.getChunkCount() == 2));

		for (std::size_t removedCount{0}; removedCount < firstChunkCapacity; ++removedCount)
		{
			static_cast<void>(archetype.removeEntity(0, 0));
		}

		WHEN("the empty chunk is reused and all existing chunks become full")
		{
			for (std::size_t addedCount{0}; addedCount < (firstChunkCapacity * 2) - 1; ++addedCount)
			{
				++nextEntityIndex;
				static_cast<void>(archetype.addEntity(
					Entity{.index = static_cast<unsigned int>(nextEntityIndex), .generation = 1}, copyData, moveData));
			}

			++nextEntityIndex;
			auto location{archetype.addEntity(Entity{.index = static_cast<unsigned int>(nextEntityIndex), .generation = 1}, copyData, moveData)};

			THEN("a new chunk is allocated without resetting a live reused chunk")
			{
				CHECK((location.first == 2));
				CHECK((location.second == 0));
				CHECK((archetype.getChunkCount() == 3));
				CHECK((archetype.getEntityCount(0) == firstChunkCapacity));
				CHECK((archetype.getEntityCount(1) == firstChunkCapacity));
			}
		}
	}
}

SCENARIO("Archetype transactional row construction")
{
	using Dimensia::Components::Position;
	using Dimensia::Components::Velocity;
	using Dimensia::ECS::Archetype;
	using Dimensia::ECS::ComponentMask;
	using Dimensia::ECS::Entity;
	using Dimensia::Registry::componentID;
	using Dimensia::Registry::MAX_COMPONENTS;

	GIVEN("a row whose second component has no construction source")
	{
		ComponentMask mask{0};
		mask.setBit(componentID<Position>());
		mask.setBit(componentID<Velocity>());
		Archetype archetype{mask, 1};
		Position position{.x = 1.0F, .y = 2.0F, .z = 3.0F};
		Velocity velocity{.dx = 4.0F, .dy = 5.0F, .dz = 6.0F};
		std::array<const void *, MAX_COMPONENTS> copyData{};
		std::array<void *, MAX_COMPONENTS> moveData{};
		copyData.at(componentID<Position>()) = &position;

		WHEN("row construction fails after constructing the first component")
		{
			auto addIncompleteRow = [&] { return archetype.addEntity(Entity{.index = 1, .generation = 1}, copyData, moveData); };
			CHECK_THROWS_AS(addIncompleteRow(), std::invalid_argument);

			THEN("the row is rolled back and its slot remains reusable")
			{
				REQUIRE((archetype.getChunkCount() == 1));
				CHECK((archetype.getEntityCount(0) == 0));

				copyData.at(componentID<Velocity>()) = &velocity;
				auto location{archetype.addEntity(Entity{.index = 2, .generation = 1}, copyData, moveData)};

				CHECK((location.first == 0));
				CHECK((location.second == 0));
				CHECK((archetype.getEntityCount(0) == 1));
			}
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)