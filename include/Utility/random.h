/*! @file random.h
	@brief Contains the function declarations for creating a random number generator
	@date 02/12/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
*/

#ifndef INCLUDE_RANDOM_H
#define INCLUDE_RANDOM_H

#include <chrono>
#include <random>

#include "Core/attributeMacros.h"
#include "Core/cconcepts.h" // for Integral

/*! @namespace Dimensia::Utility Holds any useful functionality that doesn't fit anywhere else
	@date 02/12/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
*/
namespace Dimensia::Utility
{
	/*! @class Random random.h "include/random.h"
		@brief Class for creating a random number generator
		@date 02/12/2026
		@version 0.0.1
		@since 0.0.1
		@author Matthew Moore
	*/
	class Random
	{
		public:
			/*! @brief Gets a random number in the range [min, max] wtih a templated return type in case you need to cast the uniform
			   distribution result to a different type
				@tparam T The type to cast the uniform distribution result to
				@param[in] min The minimum value (inclusive)
				@param[in] max The maximum value (inclusive)
				@retval T The typecasted random number
				@date 02/12/2026
				@version 0.0.1
				@since 0.0.1
				@author Matthew Moore
			*/
			template <Dimensia::Core::Integral T>
			ATTR_NODISCARD static T get(const T min, const T max) noexcept
			{
				return std::uniform_int_distribution<T>{min, max}(mTwister);
			}

			/*! @brief Gets #mTwister
				@retval std::mt19937 The global random number generator
				@date 02/12/2026
				@version 0.0.1
				@since 0.0.1
				@author Matthew Moore
			*/
			ATTR_NODISCARD static std::mt19937 &getTwister() noexcept
			{
				return mTwister;
			}

		private:
			/*! @brief Creates the global random number generator
				@retval std::mt19937 The global random number generator
				@date 02/12/2026
				@version 0.0.1
				@since 0.0.1
				@author Matthew Moore
			*/
			ATTR_NODISCARD static std::mt19937 generate() noexcept
			{
				std::random_device randomDevice{};

				// Create seed_seq with high-res clock and 7 random numbers from std::random_device
				std::seed_seq seedSequence{
					static_cast<std::seed_seq::result_type>(std::chrono::steady_clock::now().time_since_epoch().count()),
					randomDevice(),
					randomDevice(),
					randomDevice(),
					randomDevice(),
					randomDevice(),
					randomDevice(),
					randomDevice(),
				};

				return std::mt19937{seedSequence};
			}

			static inline std::mt19937 mTwister{generate()}; /*!< The global random number generator */
	};
} // namespace Dimensia::Utility

#endif