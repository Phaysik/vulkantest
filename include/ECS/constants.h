/*! \file constants.h
	\brief Contains the constants for the ECS
	\date 02/17/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_CONSTANTS_H
#define INCLUDE_ECS_CONSTANTS_H
#include "Core/typedefs.h"

namespace Dimensia::ECS
{
	using Dimensia::Core::ul;

	/*! @brief Number of bits in the full (upper) bit index boundary used by `ComponentMask` operations.
		@details This value represents the bit index corresponding to the end of the upper half when treating the two halves as a contiguous
	   bitset. It is used as a threshold in shift operations to determine when shifts overflow the lower half into the upper half and when
	   the mask becomes empty.
		@note Treated as an unsigned count of bit positions.
	*/
	constexpr ul UPPER_HALF_BIT_MASK{128};

	/*! @brief Number of bits in the lower half of the `ComponentMask`.
		@details Represents the width of the lower-order half (number of bit positions stored in `mLow`). Many bit-manipulation helpers use
	   this value when moving bits between halves.
	*/
	constexpr ul LOWER_HALF_BIT_MASK{64};

	/*! @brief Default chunk size (in bytes) used for ECS storage allocations.
		@details Controls the size of memory chunks used by ECS containers. The value `16'384` corresponds to 16 KiB and is chosen as a
	   cache-friendly default; it can be tuned for specific workloads to improve locality.
		@note Units: bytes.
	*/
	constexpr std::size_t CHUNK_SIZE{16'384};

	/*! @brief Memory alignment (in bytes) used for chunk allocations.
		@details Chunks and component storage are aligned to this boundary to improve performance on modern CPUs and to satisfy alignment
	   requirements for types stored in ECS containers.
		@note Units: bytes.
	*/
	constexpr ul CHUNK_ALIGNMENT{64};

	/*! @brief Number of `ul` words used per entity to store tag bits in chunk tag bitsets.
		@details Two words (2 * 64 = 128 bits) match the full width of `ComponentMask`.
	*/
	constexpr std::size_t TAG_WORDS_PER_ENTITY{2};

} // namespace Dimensia::ECS

#endif