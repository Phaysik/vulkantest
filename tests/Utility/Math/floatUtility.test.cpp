/*! @file floatUtility.test.cpp
	@brief Google Test unit tests for Floats::approximatelyEqualAbsRel.
	@details Exercises absolute and relative epsilon paths to ensure correct behavior
	across near-zero and large-magnitude comparisons. Tests intentionally execute
	runtime code paths (non-constexpr variables) to improve coverage reporting.
	@date 02/12/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
*/

#include "Utility/Math/floatUtility.h"

#include <numbers>

#include <catch2/catch_test_macros.hpp>

using Dimensia::Utility::Math::approximatelyEqualAbsRel;

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("Math")
{
	GIVEN("approximatelyEqualAbsRel")
	{
		THEN("exact equality returns true")
		{
			CHECK(approximatelyEqualAbsRel(std::numbers::pi, std::numbers::pi));
		}

		THEN("near zero uses absolute tolerance")
		{
			CHECK(approximatelyEqualAbsRel(1e-13, 0.0));
		}

		THEN("near zero fails with tighter absolute epsilon")
		{
			// Use a tighter absolute epsilon to force a false result at runtime
			CHECK_FALSE(approximatelyEqualAbsRel(1e-13, 0.0, 1e-14));
		}

		THEN("uses relative tolerance for large values")
		{
			CHECK(approximatelyEqualAbsRel(1e9, 1e9 + 5.0)); // relative difference = 5e-9 < REL_EPSILON (1e-8)
		}

		THEN("large values outside relative tolerance")
		{
			CHECK_FALSE(approximatelyEqualAbsRel(1e9, 1e9 + 200.0)); // relative difference = 2e-7 > REL_EPSILON
		}

		THEN("symmetric behaviour")
		{
			double num1{-1000.0};
			double num2{num1 + 1e-11}; // very small relative difference

			CHECK(approximatelyEqualAbsRel(num1, num2));
			CHECK(approximatelyEqualAbsRel(num2, num1));
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)
