#include <catch2/catch_test_macros.hpp>

#include "ECS/ecs.h"

#include <cstddef>
#include <latch>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include "Components/Health/healthComponent.h"
#include "Components/Name/nameComponent.h"
#include "Components/Position/positionComponent.h"
#include "ECS/componentMask.h"
#include "ECS/componentRegistry.h"
#include "ECS/entity.h"
#include "Tags/Alive/aliveTag.h"

namespace
{
	class FailingEntityInsertionHook
	{
		public:
			void failAfter(const std::size_t successfulInsertions) noexcept
			{
				mSuccessfulInsertionsBeforeFailure = successfulInsertions;
			}

			void operator()()
			{
				if (!mSuccessfulInsertionsBeforeFailure.has_value())
				{
					return;
				}

				if (*mSuccessfulInsertionsBeforeFailure == 0)
				{
					mSuccessfulInsertionsBeforeFailure.reset();
					throw std::bad_alloc{};
				}

				--*mSuccessfulInsertionsBeforeFailure;
			}

		private:
			std::optional<std::size_t> mSuccessfulInsertionsBeforeFailure{};
	};
} // namespace

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

template <typename Component>
concept HasECSView = requires(Dimensia::ECS::ECS &ecs) { ecs.template view<Component>(); };

SCENARIO("ECS tag filtered iteration")
{
	using Dimensia::Components::Position;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;
	using Dimensia::Tags::AliveTag;

	GIVEN("an untagged row before a tagged row in the same archetype")
	{
		ECS ecs;
		Entity untaggedEntity{ecs.createEntityWith(Position{.x = 1.0F, .y = 0.0F, .z = 0.0F})};
		Entity taggedEntity{ecs.createEntityWith(Position{.x = 2.0F, .y = 0.0F, .z = 0.0F}, AliveTag{})};
		Entity matchedEntity{};
		int matchedPosition{};
		std::size_t matchCount{};

		WHEN("iterating the component and required tag")
		{
			ecs.forEach<Position, AliveTag>([&](Entity entity, Position &position, AliveTag) {
				matchedEntity = entity;
				matchedPosition = static_cast<int>(position.x);
				++matchCount;
			});

			THEN("the matching entity receives its own component row")
			{
				CHECK((matchCount == 1));
				CHECK((matchedEntity == taggedEntity));
				CHECK((matchedPosition == 2));
				CHECK(ecs.alive(untaggedEntity));
			}
		}
	}
}

SCENARIO("ECS view component constraints")
{
	using Dimensia::Tags::AliveTag;

	GIVEN("regular components and tags")
	{
		THEN("only regular components are accepted by raw views")
		{
			CHECK_FALSE(HasECSView<AliveTag>);
		}
	}
}

SCENARIO("ECS hierarchy stale handles")
{
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;
	using Dimensia::ECS::NULL_ENTITY;

	GIVEN("a destroyed child index reused by a replacement entity")
	{
		ECS ecs;
		Entity originalParent{ecs.createEntity()};
		Entity staleChild{ecs.createEntity()};
		ecs.setParent(staleChild, originalParent);
		ecs.destroyEntity(staleChild, false);

		Entity replacementChild{ecs.createEntity()};
		Entity replacementParent{ecs.createEntity()};
		REQUIRE((replacementChild.index == staleChild.index));
		ecs.setParent(replacementChild, replacementParent);

		WHEN("querying the hierarchy with the stale child handle")
		{
			THEN("the replacement entity hierarchy is not exposed")
			{
				CHECK((ecs.getParent(staleChild) == NULL_ENTITY));
				CHECK((ecs.getParent(replacementChild) == replacementParent));
			}
		}
	}

	GIVEN("a destroyed parent index reused by a replacement entity")
	{
		ECS ecs;
		Entity staleParent{ecs.createEntity()};
		ecs.destroyEntity(staleParent, false);

		Entity replacementParent{ecs.createEntity()};
		Entity replacementChild{ecs.createEntity()};
		REQUIRE((replacementParent.index == staleParent.index));
		ecs.setParent(replacementChild, replacementParent);

		WHEN("querying children with the stale parent handle")
		{
			THEN("the replacement entity children are not exposed")
			{
				CHECK(ecs.getChildren(staleParent).empty());
				CHECK((ecs.getChildren(replacementParent).size() == 1));
			}
		}
	}
}

SCENARIO("ECS batch sizing")
{
	using Dimensia::ECS::ECS;

	GIVEN("an empty chunk list")
	{
		WHEN("calculating a batch size")
		{
			std::size_t batchSize{ECS::getBatchSize(0)};

			THEN("the result remains nonzero")
			{
				CHECK((batchSize == 1));
			}
		}
	}
}

SCENARIO("ECS runtime component ID validation")
{
	using Dimensia::ECS::ComponentMask;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;
	using Dimensia::Registry::MAX_COMPONENTS;

	GIVEN("a live entity and an out-of-range component identifier")
	{
		ECS ecs;
		Entity entity{ecs.createEntity()};
		auto invalidComponentID{static_cast<Dimensia::Registry::ComponentTypeID>(MAX_COMPONENTS)};

		WHEN("the identifier crosses a runtime ECS boundary")
		{
			ComponentMask mask{0, 0};
			mask.setBit(invalidComponentID);

			THEN("the operation fails safely without changing storage")
			{
				CHECK_THROWS_AS(ecs.removeComponent(entity, invalidComponentID), std::out_of_range);
				CHECK_FALSE(mask.testBit(invalidComponentID));
				CHECK(ecs.alive(entity));
			}
		}
	}
}

SCENARIO("ECS entity creation rollback")
{
	using Dimensia::Components::Health;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;
	using Dimensia::ECS::EntityInsertionHook;

	GIVEN("a reusable entity index and a failing archetype allocation")
	{
		FailingEntityInsertionHook failingInsertion;
		ECS ecs{EntityInsertionHook::bind(failingInsertion)};
		Entity releasedEntity{ecs.createEntity()};
		ecs.destroyEntity(releasedEntity, false);

		WHEN("component entity creation throws after reserving the index")
		{
			failingInsertion.failAfter(0);
			CHECK_THROWS_AS(ecs.createEntityWith(Health{10.0F}), std::bad_alloc);
			Entity replacement{ecs.createEntity()};

			THEN("the reservation is returned to the free list")
			{
				CHECK((replacement.index == releasedEntity.index));
				CHECK(ecs.alive(replacement));
				CHECK_FALSE(ecs.alive(releasedEntity));
			}
		}
	}

	GIVEN("a fresh entity index and a failing archetype allocation")
	{
		FailingEntityInsertionHook failingInsertion;
		ECS ecs{EntityInsertionHook::bind(failingInsertion)};

		WHEN("component entity creation throws after reserving the first index")
		{
			failingInsertion.failAfter(0);
			CHECK_THROWS_AS(ecs.createEntityWith(Health{10.0F}), std::bad_alloc);
			Entity replacement{ecs.createEntity()};

			THEN("the top index is rewound for the next creation")
			{
				CHECK((replacement.index == 0));
				CHECK((replacement.generation == 1));
				CHECK(ecs.alive(replacement));
			}
		}
	}
}

SCENARIO("ECS clone rollback")
{
	using Dimensia::Components::Name;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;
	using Dimensia::ECS::EntityInsertionHook;

	GIVEN("a source with an allocating component and a reusable entity index")
	{
		FailingEntityInsertionHook failingInsertion;
		ECS ecs{EntityInsertionHook::bind(failingInsertion)};
		std::string sourceName(256, 'n');
		Entity source{ecs.createEntityWith(Name{sourceName})};
		Entity releasedEntity{ecs.createEntity()};
		ecs.destroyEntity(releasedEntity, false);

		WHEN("copying the clone component throws")
		{
			failingInsertion.failAfter(0);
			CHECK_THROWS_AS(ecs.cloneEntity(source), std::bad_alloc);
			Entity replacement{ecs.createEntity()};
			std::size_t namedEntities{};
			ecs.forEach<Name>([&](Entity, Name &) { ++namedEntities; });

			THEN("the partial clone is removed and the source remains intact")
			{
				REQUIRE(ecs.getComponent<Name>(source) != nullptr);
				CHECK((ecs.getComponent<Name>(source)->name == sourceName));
				CHECK((replacement.index == releasedEntity.index));
				CHECK((namedEntities == 1));
				CHECK(ecs.alive(source));
			}
		}
	}

	GIVEN("a source hierarchy whose child clone insertion will fail")
	{
		FailingEntityInsertionHook failingInsertion;
		ECS ecs{EntityInsertionHook::bind(failingInsertion)};
		Entity sourceRoot{ecs.createEntityWith(Name{"root"})};
		Entity sourceChild{ecs.createEntityWith(Name{"child"})};
		ecs.setParent(sourceChild, sourceRoot);

		WHEN("hierarchy cloning fails after inserting the cloned root")
		{
			failingInsertion.failAfter(1);
			CHECK_THROWS_AS(ecs.cloneEntity(sourceRoot, true), std::bad_alloc);
			std::size_t namedEntities{};
			ecs.forEach<Name>([&](Entity, Name &) { ++namedEntities; });

			THEN("all partial clones are destroyed and the source hierarchy remains intact")
			{
				CHECK((namedEntities == 2));
				CHECK(ecs.alive(sourceRoot));
				CHECK(ecs.alive(sourceChild));
				CHECK((ecs.getParent(sourceChild) == sourceRoot));
				CHECK((ecs.getChildren(sourceRoot).size() == 1));
			}
		}
	}
}

SCENARIO("ECS structural access protection")
{
	using Dimensia::Components::Health;
	using Dimensia::Components::Position;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;
	using Dimensia::ECS::ExecutionPolicy;

	GIVEN("an active forEach callback")
	{
		ECS ecs;
		Entity entity{ecs.createEntityWith(Position{.x = 1.0F, .y = 2.0F, .z = 3.0F})};

		WHEN("the callback attempts a structural mutation")
		{
			auto mutateDuringIteration = [&] {
				ecs.forEach<Position>([&](Entity callbackEntity, Position &) { ecs.addComponent(callbackEntity, Health{100.0F}); });
			};

			THEN("the mutation is rejected without changing the entity")
			{
				CHECK_THROWS_AS(mutateDuringIteration(), std::logic_error);
				CHECK_FALSE(ecs.hasComponent<Health>(entity));
				CHECK(ecs.alive(entity));
			}
		}
	}

	GIVEN("an active parallel forEach callback")
	{
		ECS ecs;
		Entity entity{ecs.createEntityWith(Position{.x = 1.0F, .y = 2.0F, .z = 3.0F})};

		WHEN("a worker callback attempts a structural mutation")
		{
			auto mutateFromWorker = [&] {
				ecs.forEach<Position>(ExecutionPolicy::Par,
									  [&](Entity callbackEntity, Position &) { ecs.addComponent(callbackEntity, Health{100.0F}); });
			};

			THEN("the worker failure is propagated to the caller")
			{
				CHECK_THROWS_AS(mutateFromWorker(), std::logic_error);
				CHECK_FALSE(ecs.hasComponent<Health>(entity));
				CHECK(ecs.alive(entity));
			}
		}
	}

	GIVEN("an active component view")
	{
		ECS ecs;
		Entity originalEntity{ecs.createEntityWith(Position{.x = 1.0F, .y = 2.0F, .z = 3.0F})};

		WHEN("the owning thread attempts a structural mutation")
		{
			auto positionView{ecs.view<Position>()};
			auto createDuringView = [&] { return ecs.createEntityWith(Position{.x = 4.0F, .y = 5.0F, .z = 6.0F}); };

			THEN("the mutation is rejected while the view remains usable")
			{
				CHECK_THROWS_AS(createDuringView(), std::logic_error);
				std::size_t viewedCount{};
				for (auto [entity, position] : positionView)
				{
					++viewedCount;
					CHECK((entity == originalEntity));
					CHECK((static_cast<int>(position.x) == 1));
				}
				CHECK((viewedCount == 1));
			}
		}
	}

	GIVEN("a view held while another thread requests a structural mutation")
	{
		ECS ecs;
		Entity originalEntity{ecs.createEntityWith(Position{.x = 1.0F, .y = 2.0F, .z = 3.0F})};
		Entity createdEntity{};
		std::latch writerStarted{1};
		std::latch writerFinished{1};
		std::jthread writer;

		WHEN("the view is released")
		{
			{
				auto positionView{ecs.view<Position>()};
				writer = std::jthread([&] {
					writerStarted.count_down();
					createdEntity = ecs.createEntityWith(Position{.x = 4.0F, .y = 5.0F, .z = 6.0F});
					writerFinished.count_down();
				});

				writerStarted.wait();
				CHECK_FALSE(writerFinished.try_wait());
				CHECK(ecs.alive(originalEntity));
			}

			writerFinished.wait();

			THEN("the waiting writer completes after the view lease ends")
			{
				CHECK(ecs.alive(createdEntity));
			}
		}
	}

	GIVEN("an exception that unwinds an active view")
	{
		ECS ecs;
		static_cast<void>(ecs.createEntityWith(Position{.x = 1.0F, .y = 2.0F, .z = 3.0F}));

		WHEN("stack unwinding destroys the view")
		{
			auto throwWithView = [&] {
				auto positionView{ecs.view<Position>()};
				static_cast<void>(positionView);
				throw std::runtime_error("test view unwinding");
			};
			CHECK_THROWS_AS(throwWithView(), std::runtime_error);

			THEN("subsequent structural mutation succeeds")
			{
				Entity createdEntity{ecs.createEntityWith(Position{.x = 4.0F, .y = 5.0F, .z = 6.0F})};
				CHECK(ecs.alive(createdEntity));
			}
		}
	}
}

SCENARIO("ECS hierarchy cycle prevention")
{
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;
	using Dimensia::ECS::NULL_ENTITY;

	GIVEN("a parent with one child")
	{
		ECS ecs;
		Entity hierarchyRoot{ecs.createEntity()};
		Entity hierarchyLeaf{ecs.createEntity()};
		ecs.setParent(hierarchyLeaf, hierarchyRoot);

		WHEN("the parent is assigned beneath its child")
		{
			ecs.setParent(hierarchyRoot, hierarchyLeaf);

			THEN("the cycle is rejected without changing the hierarchy")
			{
				CHECK((ecs.getParent(hierarchyRoot) == NULL_ENTITY));
				CHECK((ecs.getParent(hierarchyLeaf) == hierarchyRoot));
				CHECK((ecs.getChildren(hierarchyRoot).size() == 1));
				CHECK(ecs.getChildren(hierarchyLeaf).empty());
			}
		}
	}

	GIVEN("a three-level hierarchy")
	{
		ECS ecs;
		Entity root{ecs.createEntity()};
		Entity middle{ecs.createEntity()};
		Entity leaf{ecs.createEntity()};
		ecs.setParent(middle, root);
		ecs.setParent(leaf, middle);

		WHEN("the root is assigned beneath its deepest descendant")
		{
			ecs.setParent(root, leaf);

			THEN("the deep cycle is rejected and recursive operations remain safe")
			{
				CHECK((ecs.getParent(root) == NULL_ENTITY));
				CHECK((ecs.getParent(middle) == root));
				CHECK((ecs.getParent(leaf) == middle));
				CHECK_NOTHROW(ecs.destroyEntity(root, true));
				CHECK_FALSE(ecs.alive(root));
				CHECK_FALSE(ecs.alive(middle));
				CHECK_FALSE(ecs.alive(leaf));
			}
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)