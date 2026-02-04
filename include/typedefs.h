/*! @file typedefs.h
	@brief C++ file for creating type aliases.
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_TYPEDEFS_H
#define INCLUDE_TYPEDEFS_H

#include <cstdint>
#include <utility>

using ub = uint8_t;	 /*!< Shorthand for unsigned byte/char */
using us = uint16_t; /*!< Shorthand for unsigned short */
using ui = uint32_t; /*!< Shorthand for unsigned int */
using ul = uint64_t; /*!< Shorthand for unsigned long */
using sb = int8_t;	 /*!< Shorthand for signed byte */
using ss = int16_t;	 /*!< Shorthand for signed short */
using si = int32_t;	 /*!< Shorthand for signed int */
using sl = int64_t;	 /*!< Shorthand for signed long */

/*! @brief Shorthand for static_cast
	@tparam T The type to cast to
	@tparam U The type to cast from
	@param[in] &value The value to cast
	@retval T The typecasted value
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/
template <typename T, typename U>
constexpr T sc(U &&value)
{
	return static_cast<T>(std::forward<U>(value));
}

#endif