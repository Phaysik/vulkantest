/*! @file typedefs.h
	@brief C++ file for creating type aliases.
	@date 02/12/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
*/

#ifndef INCLUDE_TYPEDEFS_H
#define INCLUDE_TYPEDEFS_H

#include <cstdint>

namespace Dimensia::Core
{
	using ub = uint8_t;	 /*!< Shorthand for unsigned byte/char */
	using us = uint16_t; /*!< Shorthand for unsigned short */
	using ui = uint32_t; /*!< Shorthand for unsigned int */
	using ul = uint64_t; /*!< Shorthand for unsigned long */
	using sb = int8_t;	 /*!< Shorthand for signed byte */
	using ss = int16_t;	 /*!< Shorthand for signed short */
	using si = int32_t;	 /*!< Shorthand for signed int */
	using sl = int64_t;	 /*!< Shorthand for signed long */
} // namespace Dimensia::Core

#endif