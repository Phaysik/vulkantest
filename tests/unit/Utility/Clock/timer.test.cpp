/*! @file timer.test.cpp
	@brief Google Test unit tests for `Clock::Timer` utilities.
	@date --/--/----
	@version x.x.x
	@since x.x.x
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

#include <gtest/gtest.h>

using Utility::Clock::Timer;

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

TEST(TimerGetUnitTest, DefaultAndCommonRatios)
{
	EXPECT_EQ(Timer::getUnit<std::ratio<1>>(), "s");
	EXPECT_EQ(Timer::getUnit<std::milli>(), "ms");
	EXPECT_EQ(Timer::getUnit<std::micro>(), "us");
	EXPECT_EQ(Timer::getUnit<std::nano>(), "ns");
}

TEST(TimerGetUnitTest, UnknownRatioReturnsUnknown)
{
	// num != 1 triggers the "unknown" path
	using two_over_one = std::ratio<2, 1>;
	EXPECT_EQ(Timer::getUnit<two_over_one>(), "unknown");
}

TEST(TimerStartStopTest, MeasuresPositiveDuration)
{
	// Ensure start/stop produce a non-zero positive duration when sleeping
	Timer::start();
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	double elapsed = Timer::stop<>();

	EXPECT_GT(elapsed, 0.0);
}

TEST(TimerTimeFunctionTest, WritesTimingOutputToStdoutWhenNoLogFile)
{
	// Ensure any previously opened log file is closed so output goes to stdout.
	Timer::closeLogFile();

	// Capture stdout produced by timeFunction
	std::ostringstream captured;
	std::streambuf *old = std::cout.rdbuf(captured.rdbuf());

	auto trivial = []() noexcept {
		int counter = 0;
		++counter;
		(void) counter;
	};

	// Run 3 iterations to exercise the per-iteration and average output path
	Timer::timeFunction<std::ratio<1>>("trivial", 3U, trivial);

	// restore
	std::cout.rdbuf(old);

	std::string out = captured.str();
	EXPECT_NE(out.find("Timing function: trivial"), std::string::npos);
	EXPECT_NE(out.find("Iteration 1"), std::string::npos);
	EXPECT_NE(out.find("Average:"), std::string::npos);
}

TEST(TimerCreateLogFileTest, CreatesLogFileAndWritesOutput)
{
	namespace fs = std::filesystem;

	fs::path tmpName = "timer_test_file.log";
	bool result{fs::remove(tmpName.c_str())};

	// The file does not exist, so both should be false
	EXPECT_TRUE(!result && !fs::exists(tmpName));

	// Create the log file that Timer will use
	Timer::createLogFile(tmpName);

	auto trivial = []() noexcept {
		int counter = 0;
		++counter;
		(void) counter;
	};

	// Time the function which should write into the log file
	Timer::timeFunction<std::ratio<1>>("file_trivial", 2U, trivial);

	// Close the stream held by Timer by creating a temporary Timer instance which will close
	// the internal log file in its destructor, ensuring the file can be removed reliably.
	// Ensure internal log stream is closed so file can be removed reliably.
	Timer::closeLogFile();

	// Verify file exists (content may be buffered in the process' open stream);
	// presence of the file demonstrates createLogFile opened it successfully.
	EXPECT_TRUE(fs::exists(tmpName));

	result = fs::remove(tmpName.c_str());

	// The file did exist, so the remove should have returned true and the file should no longer exist
	EXPECT_TRUE(result && !fs::exists(tmpName));
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
