/*! @file random.h
	@brief Contains the function declarations for creating a random number generator
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_RANDOM_H
#define INCLUDE_RANDOM_H

#include <chrono>
#include <random>

#include "attributeMacros.h"
#include "cconcepts.h" // for Integral
#include "typedefs.h"

/*! @namespace Utility Holds any useful functionality that doesn't fit anywhere else
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/
namespace Utility
{
	/*! @class Random random.h "include/random.h"
		@brief Class for creating a random number generator
		@date --/--/----
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	class Random
	{
		public:
			/*! @brief Gets a random number in the range [min, max]
				@param[in] min The minimum value (inclusive)
				@param[in] max The maximum value (inclusive)
				@retval int The random number in the range
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			ATTR_NODISCARD static int get(int min, int max) noexcept
			{
				return std::uniform_int_distribution{min, max}(mTwister);
			}

			/*! @brief Gets a random number in the range [min, max] wtih a templated return type in case you need to cast the uniform
			   distribution result to a different type
				@tparam T The type to cast the uniform distribution result to
				@param[in] min The minimum value (inclusive)
				@param[in] max The maximum value (inclusive)
				@retval T The typecasted random number
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			template <Concepts::Integral T>
			ATTR_NODISCARD static T get(T min, T max) noexcept
			{
				return std::uniform_int_distribution<T>{min, max}(mTwister);
			}

			/*! @brief Gets #mTwister
				@retval std::mt19937 The global random number generator
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			ATTR_NODISCARD static std::mt19937 &getTwister() noexcept
			{
				return mTwister;
			}

		private:
			/*! @brief Creates the global random number generator
				@retval std::mt19937 The global random number generator
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			ATTR_NODISCARD static std::mt19937 generate() noexcept
			{
				std::random_device randomDevice{};

				// Create seed_seq with high-res clock and 7 random numbers from std::random_device
				std::seed_seq seedSequence{sc<std::seed_seq::result_type>(std::chrono::steady_clock::now().time_since_epoch().count()),
										   randomDevice(),
										   randomDevice(),
										   randomDevice(),
										   randomDevice(),
										   randomDevice(),
										   randomDevice(),
										   randomDevice()};

				return std::mt19937{seedSequence};
			}

			static inline std::mt19937 mTwister{generate()}; /*!< The global random number generator */
	};
} // namespace Utility

#endif