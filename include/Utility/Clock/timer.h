/*! @file timer.h
	@brief Contains the function declarations for creating a class to time code execution.
	@date 02/12/2026
	@version 0.0.1
	@since 0.0.1
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

#include "Core/attributeMacros.h"
#include "Core/typedefs.h"

/*! @namespace Dimensia::Utility::Clock Holds any useful functionality that doesn't fit anywhere else
	@date 02/12/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
*/
namespace Dimensia::Utility::Clock
{
	using Dimensia::Core::ub;

	template <typename T>
	concept Ratio = std::is_same_v<T, std::ratio<T::num, T::den>>; /*!< A concept to check if a type is a std::ratio */

	/*! @enum TimeUnit A collection of named units of time
		@date 02/12/2026
		@version 0.0.1
		@since 0.0.1
		@author Matthew Moore
	*/
	enum class TimeUnit : Dimensia::Core::ui
	{
		Seconds = 1,
		Milliseconds = 1'000,
		Microseconds = 1'000'000,
		Nanoseconds = 1'000'000'000,
	};

	/*! @class Timer timer.h "include/timer.h"
		@brief A class to time code execution
		@date 02/12/2026
		@version 0.0.1
		@since 0.0.1
		@author Matthew Moore
	*/
	class Timer
	{
		public:
			// MARK: Constructors & Destructor

			// Do not allow copies or moves for this class

			Timer() = default;
			Timer(const Timer &) = delete ("Timer is not copyable");
			Timer(Timer &&) = delete ("Timer is not movable");
			Timer &operator=(const Timer &) = delete ("Timer is not copyable");
			Timer &operator=(Timer &&) = delete ("Timer is not movable");
			~Timer() = default;

			// MARK: Getters

			/*! @brief Gets the time unit for the template parameter @p T
				@pre The template parameter @p T must be a std::ratio type
				@tparam T A parameter of type std::ratio defaulted to std::ratio<1L> or per second
				@retval std::string_view The unit of time for the template parameter @p T
				@date 02/12/2026
				@version 0.0.1
				@since 0.0.1
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
				@date 02/12/2026
				@version 0.0.1
				@since 0.0.1
				@author Matthew Moore
			*/
			static void createLogFile(const std::string &filename = "timer.log") noexcept
			{
				getFileName() = filename;

				getLogFile(&filename);
			}

			/*! @brief Closes the currently opened log file (if any) and resets the stored file name.
				@post Any open internal log file is closed and future calls to `createLogFile` may reopen a file.
				@date 02/12/2026
				@version 0.0.1
				@since 0.0.1
				@author Matthew Moore
			*/
			static void closeLogFile() noexcept
			{
				if (std::ofstream &logFile = getLogFile(); logFile.is_open())
				{
					logFile.close();
				}

				getFileName() = "null";
			}

			/*! @brief Sets #mStart to the current time
				@post #mStart is set to Clock::now()
				@date 02/12/2026
				@version 0.0.1
				@since 0.0.1
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
				@date 02/12/2026
				@version 0.0.1
				@since 0.0.1
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
				@date 02/12/2026
				@version 0.0.1
				@since 0.0.1
				@author Matthew Moore
			*/
			template <Ratio T = std::ratio<1L>, typename Callable, typename... Args>
				requires(std::is_invocable_v<Callable, Args...>)
			static void timeFunction(std::string_view identifier, const ub iterations, Callable &&function, Args &&...args)
			{
				std::ofstream &logFile = getLogFile();

				std::ostream &output = logFile.is_open() ? logFile : std::cout;

				// LCOV_EXCL_BR_START — uncovered branches are compiler-generated throw edges from std::format / operator<< (std::bad_alloc)
				output << std::format("Timing function: {}\n", identifier);
				// LCOV_EXCL_BR_STOP

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

					// LCOV_EXCL_BR_START — uncovered branches are compiler-generated throw edges from std::format / operator<<
					// (std::bad_alloc)
					output << std::format("\tIteration {}: {}{}\n", i + 1, duration, unit);
					// LCOV_EXCL_BR_STOP
				}

				if (iterations > 1)
				{
					// LCOV_EXCL_BR_START — uncovered branches are compiler-generated throw edges from std::format / operator<<
					// (std::bad_alloc)
					output << std::format("\tAverage: {}{}\n", average / static_cast<double>(iterations), unit);
					// LCOV_EXCL_BR_STOP
				}
			}

		private:
			// MARK: Private Utility

			/*! @brief Sets #mFunctionStart to the current time
				@post #mFunctionStart is set to Clock::now()
				@date 02/12/2026
				@version 0.0.1
				@since 0.0.1
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
				@date 02/12/2026
				@version 0.0.1
				@since 0.0.1
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
				@date 02/12/2026
				@version 0.0.1
				@since 0.0.1
				@author Matthew Moore
			*/
			static std::ofstream &getLogFile(const std::string *filename = nullptr) noexcept
			{

				static std::ofstream mLogFile; // LCOV_EXCL_BR_LINE — fourth branch is the __cxa_atexit destructor-registration failure
											   // path, only reachable on OOM
				static std::mutex logMutex;
				if (filename != nullptr || getFileName() != "null")
				{
					const std::scoped_lock lock(logMutex);

					if (!mLogFile.is_open())
					{

						mLogFile.open((filename != nullptr) ? *filename : getFileName());
					}
				}

				return mLogFile;
			}

		private:
			using Clock = std::chrono::steady_clock;

			static inline std::chrono::time_point<Clock> mStart{Clock::now()}; /*!< The starting time for the classes internal timer */
			static inline std::chrono::time_point<Clock> mFunctionStart{Clock::now()}; /*!< The starting time for timing a function */
			static inline std::string_view mUnit{"s"};								   /*!< The unit of time for what is being timed */

			/*! @brief Provides access to the function-local static file name string.
				@return A reference to the stored file name. The reference remains valid for the lifetime of the program.
			*/
			static std::string &getFileName() noexcept
			{
				static std::string fileName{"null"}; // LCOV_EXCL_BR_LINE — fourth branch is the __cxa_atexit destructor-registration
													 // failure path, only reachable on OOM
				return fileName;
			}
	};
} // namespace Dimensia::Utility::Clock

#endif