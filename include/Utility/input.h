/*! @file input.h
	@brief Contains the function declarations for getting user input.
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_INPUT_H
#define INCLUDE_INPUT_H

#include <array>
#include <concepts>
#include <cstdlib> // for std::exit
#include <functional>
#include <iostream>
#include <limits> // for std::numeric_limits
#include <print>
#include <string>
#include <type_traits>
#include <vector>

#include "cconcepts.h" // for Integral, String

/*! @namespace Utility Holds any useful functionality that doesn't fit anywhere else
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/
namespace Utility
{
	template <typename T>
	concept IsArrayLike = requires {
		typename T::value_type; // Requires the type to have a nested `value_type`
		requires(!std::same_as<std::remove_cvref_t<T>, std::string>)
					&& (std::same_as<std::remove_cvref_t<T>, std::vector<typename T::value_type>>
						|| (std::same_as<std::remove_cvref_t<T>, std::array<typename T::value_type, std::tuple_size_v<T>>>) );
	}; /*!< Type trait to check if T is an array-like type */

	/*! @class Input input.h "include/input.h"
		@brief Will try and extract valid user input and clean up the input buffer as needed
		@date --/--/----
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	class Input
	{
		public:
			/*! @brief Will try to extract a value of type T from the user until it succeeds
				@pre @p T must have an overloaded operator>> operator.
				@post The input stream will be empty
				@tparam T The type of the input desired
				@param[in] inputMessage The message to print to the user. The default value is #mInputMessage
				@param[in] errorMessage If the input extraction fails, this message will be printed to the user. The default value is
			   #mErrorMessage
				@param[in] ignoreExtraneous Determines whether to ignore extraneous input in the input buffer. The default value is true
				@param[in, out] input The input stream to use. The default value is std::cin
				@param[in] afterFailureOnly If you need to re-print the input message after a condition has failed. The default value is
			   false
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			template <typename T>
			static T getInput(std::string_view inputMessage = mInputMessage, std::string_view errorMessage = mErrorMessage,
							  const bool ignoreExtraneous = true, std::istream &input = std::cin, const bool afterFailureOnly = false)
			{
				while (true) // Loop until user enters a valid input
				{
					if (!afterFailureOnly)
					{
						printIfNotEmpty(inputMessage);
					}

					T userInput{};
					input >> userInput;

					if (clearFailedExtraction(input))
					{
						printIfNotEmpty(errorMessage, true);

						continue;
					}

					if (ignoreExtraneous)
					{
						if (input.peek() != '\n')
						{
							printIfNotEmpty(errorMessage, true);

							ignoreLine(input);
							continue;
						}

						ignoreLine(input); // Remove any extraneous input
					}

					return userInput; // Return the value we extracted
				}
			}

			/*! @brief Will try to extract a value of type std::string from the user until it succeeds
				@post The input stream will be empty
				@tparam T std::string
				@param[in] inputMessage The message to print to the user. The default value is #mInputMessage
				@param[in] errorMessage If the input extraction fails, this message will be printed to the user. The default value is
			   #mErrorMessage
				@param[in] ignoreExtraneous Not really applicable in this situation as we use std::getline to grab the input
				@param[in, out] input The input stream to use. The default value is std::cin
				@param[in] afterFailureOnly If you need to re-print the input message after a condition has failed. The default value is
			   false
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			template <Concepts::String T>
			static T getInput(std::string_view inputMessage = mInputMessage, std::string_view errorMessage = mErrorMessage,
							  [[maybe_unused]] const bool ignoreExtraneous = true, std::istream &input = std::cin,
							  const bool afterFailureOnly = false)
			{
				while (true) // Loop until user enters a valid input
				{
					if (!afterFailureOnly)
					{
						printIfNotEmpty(inputMessage);
					}

					T userInput{};
					std::getline(input >> std::ws, userInput);

					if (clearFailedExtraction(input))
					{
						printIfNotEmpty(errorMessage, true);

						continue;
					}

					return userInput; // Return the value we extracted
				}
			}

			/*! @brief A generic function for getting user input with a bounded min and max specifically for integral types
				@pre The template type @p T must be an integral type
				@tparam T The integral type
				@param[in] min The minimum value that is acceptable
				@param[in] max The maximum value that is acceptable
				@param[in] inputMessage The message to print to the user. The default value is #mInputMessage
				@param[in] errorMessage If the input extraction fails, this message will be printed to the user. The default value is
			   #mErrorMessage
				@param[in] ignoreExtraneous Determines whether to ignore extraneous input in the input buffer. The default value is true
				@param[in, out] input The input stream to use. The default value is std::cin
				@param[in] afterFailureOnly If you need to re-print the input message after a condition has failed. The default value is
			   false
				@retval T The value that was extracted
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			template <Concepts::Integral T>
			static T getInput(const T min, const T max, std::string_view inputMessage = mInputMessage,
							  std::string_view errorMessage = mErrorMessage, const bool ignoreExtraneous = true,
							  std::istream &input = std::cin, const bool afterFailureOnly = false)
			{
				T userInput{getInput<T>(inputMessage, errorMessage, ignoreExtraneous, input, afterFailureOnly)};

				while (true)
				{
					if (userInput < min || userInput > max)
					{
						std::println("{} was not in the range of [{}, {}].", userInput, min, max);
						userInput = getInput<T>(inputMessage, errorMessage, ignoreExtraneous, input, !afterFailureOnly);
					}
					else
					{
						break;
					}
				}

				return userInput;
			}

			/*! @brief A generic function for getting user input that is within some array-like object
				@pre The template type @p T must be either a std::array or std::vector
				@tparam T An array-like object
				@param[in] inputMessage The message to print to the user. The default value is #mInputMessage
				@param[in] errorMessage If the input extraction fails, this message will be printed to the user. The default value is
			   #mErrorMessage
				@param[in] ignoreExtraneous Determines whether to ignore extraneous input in the input buffer. The default value is true
				@param[in, out] input The input stream to use. The default value is std::cin
				@param[in] afterFailureOnly If you need to re-print the input message after a condition has failed. The default value is
			   false
				@retval T::value_type The value type of the array-like object
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			template <IsArrayLike T>
			static T::value_type getInput(const T &array, std::string_view inputMessage = mInputMessage,
										  std::string_view errorMessage = mErrorMessage, const bool ignoreExtraneous = true,
										  std::istream &input = std::cin, const bool afterFailureOnly = false)
			{
				using TValueType = T::value_type;
				TValueType userInput{getInput<TValueType>(inputMessage, errorMessage, ignoreExtraneous, input, afterFailureOnly)};

				while (true)
				{
					if (std::ranges::find(array, userInput) == array.end())
					{
						std::println("{} within the provided array-like object.", userInput);
						userInput = getInput<TValueType>(inputMessage, errorMessage, ignoreExtraneous, input, !afterFailureOnly);
					}
					else
					{
						break;
					}
				}

				return userInput;
			}

			/*! @brief A generic function for getting user input that is validated by some function that returns a boolean
				@tparam T The type of the input desired
				@param[in] inputMessage The message to print to the user. The default value is #mInputMessage
				@param[in] errorMessage If the input extraction fails, this message will be printed to the user. The default value is
			   #mErrorMessage
				@param[in] ignoreExtraneous Determines whether to ignore extraneous input in the input buffer. The default value is true
				@param[in, out] input The input stream to use. The default value is std::cin
				@param[in] afterFailureOnly If you need to re-print the input message after a condition has failed. The default value is
			   false
				@retval T The value that was extracted
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			template <typename T>
			static T getInput(const std::function<bool(T)> &func, std::string_view inputMessage = mInputMessage,
							  std::string_view errorMessage = mErrorMessage, const bool ignoreExtraneous = true,
							  std::istream &input = std::cin, const bool afterFailureOnly = false)
			{
				T userInput{getInput<T>(inputMessage, errorMessage, ignoreExtraneous, input, afterFailureOnly)};

				while (true)
				{
					if (!func(userInput))
					{
						std::println("{} did not meet the conditions laid out by the provided function.", userInput);
						userInput = getInput<T>(inputMessage, errorMessage, ignoreExtraneous, input, !afterFailureOnly);
					}
					else
					{
						break;
					}
				}

				return userInput;
			}

		private:
			static constexpr std::string_view mInputMessage{"Please enter a value: "};			 /*!< A generic input requesting message */
			static constexpr std::string_view mErrorMessage{"Invalid input. Please try again."}; /*!< A generic error message */

			/*! @brief This function will clear any extraneous input in the input buffer
				@post The input buffer will be empty
				@param[in] input The input stream to use.
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			static void ignoreLine(std::istream &input) noexcept
			{
				input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
			}

			/*! @brief Tests for extraction failed, input stream closing, and will handle those by putting the input stream back into normal
			   operation mode and removing the bad input
				@post Potentially ends the program if the input stream was closed, otherwise puts the input stream back into
			   normal operation mode
				@param[in] input The input stream to use.
				@retval bool If the extraction failed
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			static bool clearFailedExtraction(std::istream &input)
			{
				// Check for failed extraction
				if (!input) // If the previous extraction failed
				{
					if (input.eof()) // If the stream was closed
					{
						// NOLINTNEXTLINE
						exit(0); // Shut down the program now
					}

					// Let's handle the failure
					input.clear();	   // Put us back in 'normal' operation mode
					ignoreLine(input); // And remove the bad input

					return true;
				}

				return false;
			}

			/*! @brief Will print out @p message provided that it is not empty
				@param[in] message The message to print
				@param[in] newLine If a new line should be printed
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			static void printIfNotEmpty(std::string_view message, const bool newLine = false)
			{
				if (!message.empty())
				{
					std::print("{}", message);

					if (newLine)
					{
						std::println();
					}
				}
			}
	};
} // namespace Utility

#endif