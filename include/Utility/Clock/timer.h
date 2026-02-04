/*! @file timer.h
	@brief Contains the function declarations for creating a class to time code execution.
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_CLOCK_H
#define INCLUDE_CLOCK_H

#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <ratio>
#include <string_view>
#include <utility>

#include "attributeMacros.h"
#include "typedefs.h"

/*! @namespace Utility::Clock Holds any useful functionality that doesn't fit anywhere else
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/
namespace Utility::Clock
{
	template <typename T>
	concept Ratio = std::is_same_v<T, std::ratio<T::num, T::den>>; /*!< A concept to check if a type is a std::ratio */

	/*! @enum TimeUnit A collection of named units of time
		@date --/--/----
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	enum class TimeUnit : ui
	{
		Seconds = 1,
		Milliseconds = 1'000,
		Microseconds = 1'000'000,
		Nanoseconds = 1'000'000'000
	};

	/*! @class Timer timer.h "include/timer.h"
		@brief A class to time code execution
		@date --/--/----
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	class Timer
	{
		public:
			// MARK: Constructors & Destructor

			/*! @brief Closes the log file if it is open
				@post The log file is closed if it is open
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			~Timer() noexcept
			{
				std::ofstream &logFile = getLogFile();

				if (logFile.is_open())
				{
					logFile.close();
				}
			}

			// Do not allow copies or moves for this class

			Timer(const Timer &) = delete;
			Timer(Timer &&) = delete;
			Timer &operator=(const Timer &) = delete;
			Timer &operator=(Timer &&) = delete;

			// MARK: Getters

			/*! @brief Gets the time unit for the template parameter @p T
				@pre The template parameter @p T must be a std::ratio type
				@tparam T A parameter of type std::ratio defaulted to std::ratio<1L> or per second
				@retval std::string_view The unit of time for the template parameter @p T
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			template <Ratio T = std::ratio<1L>>
			ATTR_NODISCARD static constexpr std::string_view getUnit() noexcept
			{
				if constexpr (T::num == 1) // This check is to make sure that the unit is <=1s
				{
					switch (T::den)
					{
						using enum TimeUnit;

						case std::to_underlying(TimeUnit::Seconds):
							return "s";
						case std::to_underlying(TimeUnit::Milliseconds):
							return "ms";
						case std::to_underlying(TimeUnit::Microseconds):
							return "us";
						case std::to_underlying(TimeUnit::Nanoseconds):
							return "ns";
						default:
							return "unknown";
					}
				}
				else
				{
					return "unknown";
				}
			}

			// MARK: Utility

			/*! @brief Creates and opens a log file with the name @p filename
				@post A log file with the name @p filename is created and opened
				@param[in] filename The name of the log file to create
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			static void createLogFile(const std::string &filename = "timer.log") noexcept
			{
				getLogFile(&filename);
			}

			/*! @brief Closes the currently opened log file (if any) and resets the stored file name.
				@post Any open internal log file is closed and future calls to `createLogFile` may reopen a file.
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			static void closeLogFile() noexcept
			{
				if (std::ofstream &logFile = getLogFile(); logFile.is_open())
				{
					logFile.close();
				}

				mFileName = "null";
			}

			/*! @brief Sets #mStart to the current time
				@post #mStart is set to Clock::now()
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			static void start() noexcept
			{
				mStart = Clock::now();
			}

			/*! @brief Gets the elapsed time since the start of #mStart
				@pre The template parameter @p T must be a std::ratio type
				@tparam T A parameter of type std::ratio, defaulted to std::ratio<1L> or per second
				@retval double The amount of time passed since the start of #mStart
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			template <Ratio T = std::ratio<1L>>
			ATTR_NODISCARD static double stop() noexcept
			{
				using Duration = std::chrono::duration<double, T>;
				mUnit = getUnit<T>();
				return std::chrono::duration_cast<Duration>(Clock::now() - mStart).count();
			}

			/*! @brief Times the execution of @p function @p iterations times
				@pre The template parameter @p T must be a std::ratio type and @p Callable must be invocable with @p Args
				@tparam T A parameter of type std::ratio, defaulted to std::ratio<1L> or per second
				@tparam Callable A parameter that is invocable
				@tparam Args A pack of parameters to be passed to @p Callable
				@param[in] identifier A unique name to identify the function being timed
				@param[in] iterations The number of times to run @p function
				@param[in] function The function to time
				@param[in] args The arguments to pass to @p function
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			template <Ratio T = std::ratio<1L>, typename Callable, typename... Args>
				requires(std::is_invocable_v<Callable, Args...>)
			static void timeFunction(std::string_view identifier, const ub iterations, Callable &&function, Args &&...args)
			{
				std::ofstream &logFile = getLogFile();

				std::ostream &output = logFile.is_open() ? logFile : std::cout;

				output << std::format("Timing function: {}\n", identifier);

				double average{0.0};

				constexpr std::string_view unit = getUnit<T>();

				const Callable copyFunction(std::forward<Callable>(function));
				const auto copyArgs = std::make_tuple(std::forward<Args>(args)...);

				for (ub i = 0; i < iterations; ++i)
				{
					functionStart();
					std::apply(copyFunction, copyArgs);
					const double duration = functionStop<T>();

					average += duration;

					output << std::format("\tIteration {}: {}{}\n", i + 1, duration, unit);
				}

				if (iterations > 1)
				{
					output << std::format("\tAverage: {}{}\n", average / sc<double>(iterations), unit);
				}
			}

		private:
			// MARK: Private Utility

			/*! @brief Sets #mFunctionStart to the current time
				@post #mFunctionStart is set to Clock::now()
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			static void functionStart() noexcept
			{
				mFunctionStart = Clock::now();
			}

			/*! @brief Gets the elapsed time since the start of #mFunctionStart
				@pre The template parameter @p T must be a std::ratio type
				@tparam T A parameter of type std::ratio, defaulted to std::ratio<1L> or per second
				@retval double The amount of time passed since the start of #mFunctionStart
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			template <Ratio T = std::ratio<1L>>
			ATTR_NODISCARD static double functionStop() noexcept
			{
				using Duration = std::chrono::duration<double, T>;
				return std::chrono::duration_cast<Duration>(Clock::now() - mFunctionStart).count();
			}

			/*! @brief Gets a log file to write to
				@post Will return a std::ofstream reference that may or may not be open, so make sure to check
				@param[in] filename The name of the file, defaults to nullptr
				@retval std::ofstream The log file
				@date --/--/----
				@version x.x.x
				@since x.x.x
				@author Matthew Moore
			*/
			static std::ofstream &getLogFile(const std::string *filename = nullptr) noexcept
			{
				static std::ofstream mLogFile;
				static std::mutex logMutex;
				if (filename != nullptr || mFileName != "null")
				{
					const std::scoped_lock lock(logMutex);
					if (!mLogFile.is_open())
					{
						mLogFile.open((filename != nullptr) ? *filename : std::string(mFileName));
					}
				}

				return mLogFile;
			}

		private:
			using Clock = std::chrono::steady_clock;

			static inline std::chrono::time_point<Clock> mStart{Clock::now()}; /*!< The starting time for the classes internal timer */
			static inline std::chrono::time_point<Clock> mFunctionStart{Clock::now()}; /*!< The starting time for timing a function */
			static inline std::string_view mUnit{"s"};								   /*!< The unit of time for what is being timed */
			static inline std::string_view mFileName{"null"};						   /*!< The unit of time for what is being timed */
	};
} // namespace Utility::Clock

#endif