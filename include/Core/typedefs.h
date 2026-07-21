/*! @file typedefs.h
	@brief C++ file for creating type aliases.
	@date 02/12/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
*/

#ifndef INCLUDE_TYPEDEFS_H
#define INCLUDE_TYPEDEFS_H

// WHEN clang supports reflection then we can uncomment this. It will compile will g++, but since clangd is the linter, it will error on
// everything due to the flag `-freflection`

#include <cstdint>
// #include <meta>
#include <stdfloat>

// #include <string_view>
// #include <typedefs.h>

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

	// template<typename E, bool Enumerable = std::meta::is_enumerable_type(^^E)>
	//   requires IsEnum<E>
	// constexpr std::string_view enum_to_string(E value) {
	//   if constexpr (Enumerable) {
	//     template for (constexpr auto e : define_static_array(enumerators_of(^^E)))
	//       if (value == [:e:])
	//         return identifier_of(e);
	//   }
	//   return "<unnamed>";
	// }
} // namespace Dimensia::Core

#endif