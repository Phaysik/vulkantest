#include "ECS/componentRegistry.h"

#include <array>
#include <cstddef>

#include "Components/Position/positionComponent.h"
#include "ECS/constants.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
	struct alignas(Dimensia::ECS::CHUNK_ALIGNMENT * 2) OverAlignedComponent
	{
			std::byte value{};
	};

	struct OversizedComponent
	{
			std::array<std::byte, Dimensia::ECS::CHUNK_SIZE> values{};
	};

	struct PotentiallyThrowingMoveComponent
	{
			PotentiallyThrowingMoveComponent() = default;
			PotentiallyThrowingMoveComponent(const PotentiallyThrowingMoveComponent &) = default;

			PotentiallyThrowingMoveComponent(PotentiallyThrowingMoveComponent &&) noexcept(false) {}

			PotentiallyThrowingMoveComponent &operator=(const PotentiallyThrowingMoveComponent &) = default;
			PotentiallyThrowingMoveComponent &operator=(PotentiallyThrowingMoveComponent &&) = default;
			~PotentiallyThrowingMoveComponent() = default;
	};
} // namespace

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("Component registry chunk storage constraints")
{
	using Dimensia::Components::Position;
	using Dimensia::Registry::has_safe_chunk_lifecycle_v;
	using Dimensia::Registry::is_chunk_storable_component_v;

	GIVEN("a normal component and components exceeding chunk storage constraints")
	{
		THEN("only the normal component can be registered for chunk storage")
		{
			CHECK(is_chunk_storable_component_v<Position>);
			CHECK_FALSE(is_chunk_storable_component_v<OverAlignedComponent>);
			CHECK_FALSE(is_chunk_storable_component_v<OversizedComponent>);
			CHECK(has_safe_chunk_lifecycle_v<Position>);
			CHECK_FALSE(has_safe_chunk_lifecycle_v<PotentiallyThrowingMoveComponent>);
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)