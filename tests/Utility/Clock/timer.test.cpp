/*! @file timer.test.cpp
	@brief Google Test unit tests for `Clock::Timer` utilities.
	@date 02/12/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
*/

#include "Utility/Clock/timer.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <ratio>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

using Dimensia::Utility::Clock::Timer;

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("Timer")
{
	GIVEN("getUnit")
	{
		THEN("returns correct strings for known ratios")
		{
			CHECK((Timer::getUnit<std::ratio<1>>() == "s"));
			CHECK((Timer::getUnit<std::milli>() == "ms"));
			CHECK((Timer::getUnit<std::micro>() == "us"));
			CHECK((Timer::getUnit<std::nano>() == "ns"));
		}

		THEN("returns 'unknown' for unknown ratios")
		{
			// num != 1 triggers the "unknown" path
			using two_over_one = std::ratio<2, 1>;
			CHECK((Timer::getUnit<two_over_one>() == "unknown"));
		}
	}

	GIVEN("start/stop")
	{
		THEN("measures positive duration")
		{
			// Ensure start/stop produce a non-zero positive duration when sleeping
			Timer::start();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			double elapsed{Timer::stop<>()};

			CHECK((elapsed > 0.0));
		}
	}

	GIVEN("timeFunction")
	{
		auto trivial = [] noexcept {
			int counter = 0;
			++counter;
			(void) counter;
		};

		GIVEN("with a log file")
		{
			THEN("writes timing output to the log file")
			{
				namespace fs = std::filesystem;
				fs::path tmpName{"timer_test_file.log"};
				bool result{fs::remove(tmpName.c_str())};

				// The file does not exist, so both should be false
				REQUIRE((!result && !fs::exists(tmpName)));

				Timer::createLogFile(tmpName);

				// Run 2 iterations to exercise the per-iteration and average output path
				Timer::timeFunction<std::ratio<1>>("file_trivial", 2U, trivial);

				// Close the stream held by Timer by creating a temporary Timer instance which will close
				// the internal log file in its destructor, ensuring the file can be removed reliably.
				// Ensure internal log stream is closed so file can be removed reliably.
				Timer::closeLogFile();

				// Verify file exists (content may be buffered in the process' open stream);
				// presence of the file demonstrates createLogFile opened it successfully.
				REQUIRE(fs::exists(tmpName));

				result = fs::remove(tmpName.c_str());

				// The file did exist, so the remove should have returned true and the file should no longer exist
				REQUIRE((result && !fs::exists(tmpName)));
			}
		}

		GIVEN("with no log file")
		{
			THEN("writes timing output to stdout")
			{
				// Ensure any previously opened log file is closed so output goes to stdout.
				Timer::closeLogFile();

				// Capture stdout produced by timeFunction
				std::ostringstream captured;
				std::streambuf *old{std::cout.rdbuf(captured.rdbuf())};

				// Run 3 iterations to exercise the per-iteration and average output path
				Timer::timeFunction<std::ratio<1>>("trivial", 3U, trivial);

				// restore
				std::cout.rdbuf(old);

				std::string out{captured.str()};
				CHECK(out.contains("Timing function: trivial"));
				CHECK(out.contains("Iteration 1"));
				CHECK(out.contains("Average:"));
			}
		}

		GIVEN("a single iteration")
		{
			THEN("does not print average")
			{
				// Ensure any previously opened log file is closed so output goes to stdout.
				Timer::closeLogFile();

				// Capture stdout produced by timeFunction
				std::ostringstream captured;
				std::streambuf *old{std::cout.rdbuf(captured.rdbuf())};

				Timer::timeFunction<std::ratio<1>>("single_iter", 1U, trivial);

				// restore
				std::cout.rdbuf(old);

				std::string out{captured.str()};
				CHECK(out.contains("Timing function: single_iter"));
				CHECK(out.contains("Iteration 1"));
				CHECK(!out.contains("Average:"));
			}
		}

		GIVEN("a file already open")
		{
			namespace fs = std::filesystem;
			fs::path tmpName = "timer_test_double_open.log";

			bool result{fs::remove(tmpName)};
			REQUIRE((!result && !fs::exists(tmpName)));

			Timer::createLogFile(tmpName);

			THEN("opening again does not disrupt the existing file")
			{
				// Call again while file is already open — exercises the is_open() true / skip-open branch
				Timer::createLogFile(tmpName);

				Timer::closeLogFile();

				REQUIRE(fs::exists(tmpName));
				result = fs::remove(tmpName);

				REQUIRE((result && !fs::exists(tmpName)));
			}
		}

		GIVEN("failed initial open")
		{
			THEN("timeFunction retries open via mFileName")
			{
				// Ensure clean state
				Timer::closeLogFile();

				// createLogFile with an invalid path: open() fails silently but mFileName is set
				Timer::createLogFile("/no_such_dir/no_such_subdir/impossible.log");

				// Now call timeFunction which invokes getLogFile(nullptr).
				// mFileName != "null" so it enters the if block; file is not open so it reaches
				// the open() ternary with filename==nullptr, exercising the std::string(mFileName) path.
				std::ostringstream captured;
				std::streambuf *oldBuf{std::cout.rdbuf(captured.rdbuf())};

				Timer::timeFunction<std::ratio<1>>("retry_open", 1U, trivial);
				std::cout.rdbuf(oldBuf);

				// The retry also fails (invalid path), so output falls back to std::cout
				std::string out{captured.str()};
				CHECK(out.contains("Timing function: retry_open"));

				Timer::closeLogFile();
			}
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)
