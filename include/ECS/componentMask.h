/*! @file componentMask.h
	@brief Component presence bitmask used by the ECS subsystem.
	@details Declares `ComponentMask`, a two-half unsigned bitmask (lower and upper halves) used to represent component presence for
   entities. The header documents the mask's bitwise operators, shift semantics, and conversion behaviour.
	@date 02/14/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMPONENTMASK_H
#define INCLUDE_ECS_COMPONENTMASK_H

#include <compare>
#include <functional>

#include "Core/attributeMacros.h"
#include "Core/typedefs.h"

#include "constants.h"

namespace Dimensia::ECS
{

	using Dimensia::Core::ul;

	/*! @struct ComponentMask include/ECS/componentMask.h
		@brief 128-bit component presence bitmask split into lower and upper halves.
		@details The type stores component presence bits across two unsigned halves: `mLow` represents the lower-order bits and `mHigh`
	   represents the higher-order bits. The struct provides constexpr bitwise operators and shifts that treat the two halves as a
	   contiguous bitset. Operators are marked `noexcept` where possible to make this type suitable for hot-path usage.
		@note Left and right shift operators move bits between `mLow` and `mHigh` to emulate a contiguous 128-bit shift. Shifting by values
	   greater than the full width yields a zero mask.
		@post Bitwise operations preserve the semantics of standard integral bitwise operators across the combined halves.
		@since 02/14/2026
		@version x.x.x
		@author Matthew Moore
	*/
	struct ComponentMask
	{
			/*! @brief Construct a mask with only the lower half specified.
				@param[in] low Initial value for the lower half (`mLow`). Upper half (`mHigh`) is zero-initialized.
				@note This constructor is explicit to avoid accidental implicit conversions from integers.
			*/
			explicit constexpr ComponentMask(const ul low) noexcept : mLow(low) {}

			/*! @brief Construct a mask with both halves specified.
				@param[in] low Initial value for the lower half (`mLow`).
				@param[in] high Initial value for the upper half (`mHigh`).
			*/
			constexpr ComponentMask(const ul low, const ul high) noexcept : mLow(low), mHigh(high) {}

			// MARK: Bitwise Operator Overloads

			/*! @brief Bitwise AND between two component masks.
				@param[in] other Mask to AND with this mask.
				@return A new `ComponentMask` where each bit is the logical AND of the corresponding bits from the operands.
			*/
			constexpr ComponentMask operator&(const ComponentMask &other) const noexcept
			{
				return {mLow & other.mLow, mHigh & other.mHigh};
			}

			/*! @brief Bitwise OR between two component masks.
				@param[in] other Mask to OR with this mask.
				@return A new `ComponentMask` where each bit is the logical OR of the corresponding bits from the operands.
			*/
			constexpr ComponentMask operator|(const ComponentMask &other) const noexcept
			{
				return {mLow | other.mLow, mHigh | other.mHigh};
			}

			/*! @brief Bitwise XOR between two component masks.
				@param[in] other Mask to XOR with this mask.
				@return A new `ComponentMask` where each bit is the logical exclusive OR of the corresponding bits from the operands.
			*/
			constexpr ComponentMask operator^(const ComponentMask &other) const noexcept
			{
				return {mLow ^ other.mLow, mHigh ^ other.mHigh};
			}

			/*! @brief Bitwise NOT (bitwise inversion) of the combined mask.
				@return A new `ComponentMask` with all bits inverted (both lower and upper halves).
			*/
			constexpr ComponentMask operator~() const noexcept
			{
				return {~mLow, ~mHigh};
			}

			/*! @brief Logical left-shift of the combined 2*|ul| bit mask.
				@param[in] shift Number of bit positions to shift left.
				@note Shifting moves bits from `mLow` into `mHigh` as appropriate.
				@return A new mask equal to this mask shifted left by @p shift.
			*/
			constexpr ComponentMask operator<<(const ul shift) const noexcept
			{
				if (shift == 0)
				{
					return *this;
				}

				if (shift < LOWER_HALF_BIT_MASK)
				{
					return {mLow << shift, (mHigh << shift) | (mLow >> (LOWER_HALF_BIT_MASK - shift))};
				}

				if (shift < UPPER_HALF_BIT_MASK)
				{
					return {0, mLow << (shift - LOWER_HALF_BIT_MASK)};
				}

				return {0, 0};
			}

			/*! @brief Logical right-shift of the combined 2*|ul| bit mask.
				@param[in] shift Number of bit positions to shift right.
				@note Shifting moves bits from `mHigh` into `mLow` as appropriate.
				@return A new mask equal to this mask shifted right by @p shift.
			*/
			constexpr ComponentMask operator>>(const ul shift) const noexcept
			{
				if (shift == 0)
				{
					return *this;
				}

				if (shift < LOWER_HALF_BIT_MASK)
				{
					return {(mLow >> shift) | (mHigh << (LOWER_HALF_BIT_MASK - shift)), mHigh >> shift};
				}

				if (shift < UPPER_HALF_BIT_MASK)
				{
					return {mHigh >> (shift - LOWER_HALF_BIT_MASK), 0};
				}

				return {0, 0};
			}

			/*! @brief Compound bitwise AND assignment.
				@param[in] other Mask to AND into this mask.
				@note This operation modifies both `mLow` and `mHigh` in-place.
				@return Reference to this mask after applying bitwise AND with @p other.
			*/
			constexpr ComponentMask &operator&=(const ComponentMask &other) noexcept
			{
				mLow &= other.mLow;
				mHigh &= other.mHigh;
				return *this;
			}

			/*! @brief Compound bitwise OR assignment.
				@param[in] other Mask to OR into this mask.
				@note This operation modifies both `mLow` and `mHigh` in-place.
				@return Reference to this mask after applying bitwise OR with @p other.
			*/
			constexpr ComponentMask &operator|=(const ComponentMask &other) noexcept
			{
				mLow |= other.mLow;
				mHigh |= other.mHigh;
				return *this;
			}

			/*! @brief Compound bitwise XOR assignment.
				@param[in] other Mask to XOR into this mask.
				@note This operation modifies both `mLow` and `mHigh` in-place.
				@return Reference to this mask after applying bitwise XOR with @p other.
			*/
			constexpr ComponentMask &operator^=(const ComponentMask &other) noexcept
			{
				mLow ^= other.mLow;
				mHigh ^= other.mHigh;
				return *this;
			}

			// MARK: Comparison Operator Overload

			/*! @brief Three-way comparison (spaceship) operator.
				@details Performs a lexicographical three-way comparison of this mask against @p other using the member declaration order.
			   First `mLow` is compared, and if equal `mHigh` is compared. The operator is `= default` and provides a total ordering
			   compatible with the standard library's comparison utilities.
				@param[in] other The mask to compare against.
				@return A `std::strong_ordering` value indicating the comparison result (`less`, `equal`, or `greater`).
			*/
			constexpr std::strong_ordering operator<=>(const ComponentMask &other) const noexcept = default;

			// MARK: Conversion Operator Overload

			/*! @brief Test whether any bit in the combined mask is set.
				@return `true` if either `mLow` or `mHigh` is non-zero, otherwise `false`.
			*/
			constexpr explicit operator bool() const noexcept
			{
				return mLow != 0 || mHigh != 0;
			}

			// MARK: Bit Manipulation

			/*! @brief Set the bit corresponding to @p id in the appropriate half of the mask.
				@param[in] typeID Component type identifier whose bit to set.
			*/
			constexpr void setBit(const ul typeID) noexcept
			{
				if (typeID < LOWER_HALF_BIT_MASK)
				{
					mLow |= (1ULL << typeID);
				}
				else
				{
					mHigh |= (1ULL << (typeID - LOWER_HALF_BIT_MASK));
				}
			}

			/*! @brief Clear the bit corresponding to @p id in the appropriate half of the mask.
				@param[in] typeID Component type identifier whose bit to clear.
			*/
			constexpr void clearBit(const ul typeID) noexcept
			{
				if (typeID < LOWER_HALF_BIT_MASK)
				{
					mLow &= ~(1ULL << typeID);
				}
				else
				{
					mHigh &= ~(1ULL << (typeID - LOWER_HALF_BIT_MASK));
				}
			}

			/*! @brief Test whether the bit corresponding to @p typeID is set.
				@param[in] typeID Component type identifier whose bit to test.
				@return `true` if the bit is set, `false` otherwise.
			*/
			ATTR_NODISCARD constexpr bool testBit(const ul typeID) const noexcept
			{
				if (typeID < LOWER_HALF_BIT_MASK)
				{
					return (mLow & (1ULL << typeID)) != 0;
				}

				return (mHigh & (1ULL << (typeID - LOWER_HALF_BIT_MASK))) != 0;
			}

			// MARK: Accessors

			/*! @brief Return the lower half of the mask.
				@return Value of the lower-order half.
			*/
			ATTR_NODISCARD constexpr ul low() const noexcept
			{
				return mLow;
			}

			/*! @brief Return the upper half of the mask.
				@return Value of the upper-order half.
			*/
			ATTR_NODISCARD constexpr ul high() const noexcept
			{
				return mHigh;
			}

		private:
			/*! @var mLow
				@brief Lower-order half of the component bitmask.
				@details Contains the least-significant `LOWER_HALF_BIT_MASK` bits of the combined mask.
			*/
			ul mLow{};

			/*! @var mHigh
				@brief Upper-order half of the component bitmask.
				@details Contains the most-significant `LOWER_HALF_BIT_MASK` bits of the combined mask.
			*/
			ul mHigh{};
	};
} // namespace Dimensia::ECS

namespace std
{
	/*! @brief Hash functor for `@ref Dimensia::ECS::ComponentMask "ComponentMask"`.
		@details Produces a combined hash value from the lower and upper halves of the mask. The implementation combines the hash of `mLow`
	   and `mHigh` (@see src/ECS/componentMask.cpp "componentMask.cpp") so `ComponentMask` can be used as a key in unordered containers like
	   `std::unordered_map` and `std::unordered_set`.
		@param[in] componentMask Mask to hash.
		@return A `size_t` hash computed from the mask's two halves.
	*/
	template <>
	struct hash<Dimensia::ECS::ComponentMask>
	{
			std::size_t operator()(const Dimensia::ECS::ComponentMask &componentMask) const noexcept
			{
				std::size_t seed{hash<Dimensia::Core::ul>{}(componentMask.low())};
				seed ^= hash<Dimensia::Core::ul>{}(componentMask.high()) + Dimensia::ECS::HASH_MIX_CONSTANT
					  + (seed << Dimensia::ECS::LEFT_SHIFT_VALUE) + (seed >> 2U);
				return seed;
			}
	};
} // namespace std

#endif