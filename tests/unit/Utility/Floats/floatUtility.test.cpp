/*! @file floatUtility.test.cpp
	@brief Google Test unit tests for Floats::approximatelyEqualAbsRel.
	@details Exercises absolute and relative epsilon paths to ensure correct behavior
	across near-zero and large-magnitude comparisons. Tests intentionally execute
	runtime code paths (non-constexpr variables) to improve coverage reporting.
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#include "Utility/Floats/floatUtility.h"

#include <numbers>

#include <gtest/gtest.h>

using Utility::Floats::approximatelyEqualAbsRel;

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

TEST(FloatUtilityApproximatelyEqualTest, ExactEqualityReturnsTrue)
{
	EXPECT_TRUE(approximatelyEqualAbsRel(std::numbers::pi, std::numbers::pi));
}

TEST(FloatUtilityApproximatelyEqualTest, NearZeroUsesAbsoluteTolerance)
{
	double lhs = 1e-13; // smaller than ABS_EPSILON (1e-12)
	double rhs = 0.0;

	EXPECT_TRUE(approximatelyEqualAbsRel(lhs, rhs));
}

TEST(FloatUtilityApproximatelyEqualTest, NearZeroFailsWithTighterAbsoluteEpsilon)
{
	double lhs = 1e-13;
	double rhs = 0.0;

	// Use a tighter absolute epsilon to force a false result at runtime
	EXPECT_FALSE(approximatelyEqualAbsRel(lhs, rhs, 1e-14));
}

TEST(FloatUtilityApproximatelyEqualTest, UsesRelativeToleranceForLargeValues)
{
	double lhs = 1e9;
	double rhs = 1e9 + 5.0; // relative difference = 5e-9 < REL_EPSILON (1e-8)

	EXPECT_TRUE(approximatelyEqualAbsRel(lhs, rhs));
}

TEST(FloatUtilityApproximatelyEqualTest, LargeValuesOutsideRelativeTolerance)
{
	double lhs = 1e9;
	double rhs = 1e9 + 200.0; // relative difference = 2e-7 > REL_EPSILON

	EXPECT_FALSE(approximatelyEqualAbsRel(lhs, rhs));
}

TEST(FloatUtilityApproximatelyEqualTest, SymmetricBehaviour)
{
	double num1 = -1000.0;
	double num2 = num1 + 1e-11; // very small relative difference

	EXPECT_TRUE(approximatelyEqualAbsRel(num1, num2));
	EXPECT_TRUE(approximatelyEqualAbsRel(num2, num1));
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
