/*! @file cconcepts.test.cpp
	@brief C++ file for creating tests for validating custom concepts.
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#include "cconcepts.h"

#include <cstdint>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

using Concepts::FloatingPoint;
using Concepts::Integral;
using Concepts::RationalNumber;
using Concepts::SignedIntegral;
using Concepts::String;
using Concepts::UnsignedIntegral;

// Compile-time sanity checks (will fail to compile if concepts change unexpectedly)
static_assert(Integral<int>);
static_assert(Integral<bool>);
static_assert(FloatingPoint<double>);
static_assert(!FloatingPoint<int>);
static_assert(RationalNumber<int>);
static_assert(RationalNumber<double>);
static_assert(String<std::string>);
static_assert(String<const std::string &>);
static_assert(!String<const char *>);

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

TEST(IntegralTest, BasicIntegralTypes)
{
	constexpr bool int_ok = Integral<int>;
	constexpr bool long_ok = Integral<int64_t>;
	constexpr bool bool_ok = Integral<bool>;

	EXPECT_TRUE(int_ok);
	EXPECT_TRUE(long_ok);
	EXPECT_TRUE(bool_ok);
}

TEST(SignedUnsignedTest, SignedAndUnsigned)
{
	constexpr bool signed_ll = SignedIntegral<int64_t>;
	constexpr bool unsigned_ui = UnsignedIntegral<unsigned int>;
	constexpr bool unsigned_uc = UnsignedIntegral<unsigned char>;

	EXPECT_TRUE(signed_ll);
	EXPECT_TRUE(unsigned_ui);
	EXPECT_TRUE(unsigned_uc);

	EXPECT_FALSE(UnsignedIntegral<int>);
	EXPECT_FALSE(SignedIntegral<unsigned int>);
}

TEST(FloatingPointTest, FloatingPointTypes)
{
	EXPECT_TRUE(FloatingPoint<float>);
	EXPECT_TRUE(FloatingPoint<double>);
	EXPECT_FALSE(FloatingPoint<int>);
}

TEST(RationalNumberTest, CoversIntegralAndFloating)
{
	EXPECT_TRUE(RationalNumber<int>);
	EXPECT_TRUE(RationalNumber<double>);
	// Note: bool is considered integral by std::is_integral and therefore rational here
	EXPECT_TRUE(RationalNumber<bool>);
	EXPECT_FALSE(RationalNumber<std::string>);
}

// NOLINTBEGIN(hicpp-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

TEST(StringTest, StdStringAndCvRefVariants)
{
	EXPECT_TRUE(String<std::string>);
	EXPECT_TRUE(String<const std::string &>);
	EXPECT_TRUE(String<std::string &&>);

	EXPECT_FALSE(String<std::string_view>);
	EXPECT_FALSE(String<const char *>);
	EXPECT_FALSE(String<char[6]>);
}

// NOLINTEND(hicpp-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)