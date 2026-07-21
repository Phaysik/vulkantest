/*! @file logger.test.cpp
	@brief Catch2 BDD unit tests for the static Logger wrapper.
	@details Exercises initialization, level get/set, name/file replacement, and all seven logging-level template methods using Catch2's BDD
   syntax. Each test scenario manages temporary files that are cleaned up after the scenario completes.
	@date 02/12/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
*/

#include "Utility/Debug/Logging/logger.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Core/attributeMacros.h"
#include "Core/typedefs.h"
#include "Utility/Debug/Logging/constants.h"

#include <catch2/catch_test_macros.hpp>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace Logging = Dimensia::Utility::Debug::Logging;

using Dimensia::Core::ub;
using Logging::Logger;

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

namespace
{
	/*! @brief Flushes spdlog and reads the full contents of a log file.
		@param fileName Optional path to a specific log file. If nullptr, uses the default test log file.
		@return The file contents as a string.
	*/
	ATTR_NODISCARD std::string readLogFile(const std::string *fileName = nullptr) // NOLINT(llvm-prefer-static-over-anonymous-namespace)
	{
		std::string defaultFileName{"logger_test_output.log"};
		// Flush spdlog to ensure all output is written before reading
		spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });

		std::ifstream file(fileName != nullptr ? *fileName : defaultFileName);
		std::ostringstream contents;
		contents << file.rdbuf();
		return contents.str();
	}
} // namespace

SCENARIO("Logger")
{
	// Scenario-level setup: initialize a fresh logger for testing
	const std::string loggerName{"test_logger"};
	const std::string logFileName{"logger_test_output.log"};

	spdlog::drop_all();

	bool fileRemoved{std::filesystem::remove(logFileName)};
	REQUIRE(!fileRemoved);

	bool loggerInitialized{Logger::initialize(loggerName, logFileName)};
	REQUIRE(loggerInitialized);

	GIVEN("initialization")
	{
		THEN("logger is usable after initialization")
		{
			// Logger was initialized above; logging should succeed
			std::optional<std::string_view> result{Logger::info("initialization smoke test")};
			CHECK_FALSE(result.has_value());
		}

		THEN("re-initialization with different name and file succeeds")
		{
			std::string secondFile{"logger_test_reinit.log"};
			bool initResult{Logger::initialize("reinit_logger", secondFile)};
			CHECK(initResult);

			std::optional<std::string_view> result{Logger::info("after reinit")};
			CHECK_FALSE(result.has_value());

			// Clean up the second file
			spdlog::drop_all();

			bool secondFileRemoved{std::filesystem::remove(secondFile)};
			REQUIRE(secondFileRemoved);

			bool loggerReinitialized{Logger::initialize(loggerName, logFileName)};
			CHECK(loggerReinitialized);
		}

		THEN("re-initialization with same name succeeds")
		{
			// Re-initializing with the same name should succeed because initialize drops the old registry entry.
			bool initResult{Logger::initialize(loggerName, logFileName)};
			CHECK(initResult);

			std::optional<std::string_view> result{Logger::info("after same-name reinit")};
			CHECK_FALSE(result.has_value());
		}
	}

	GIVEN("getLevel and setLevel operations")
	{
		THEN("default level is info")
		{
			// spdlog's default level is info
			CHECK((Logger::getLevel() == spdlog::level::info));
		}

		THEN("setting debug level returns debug when queried")
		{
			Logger::setLevel(spdlog::level::debug);
			CHECK((Logger::getLevel() == spdlog::level::debug));

			Logger::setLevel(spdlog::level::info);
		}

		THEN("setting trace level returns trace when queried")
		{
			Logger::setLevel(spdlog::level::trace);
			CHECK((Logger::getLevel() == spdlog::level::trace));

			Logger::setLevel(spdlog::level::info);
		}

		THEN("setting critical level returns critical when queried")
		{
			Logger::setLevel(spdlog::level::critical);
			CHECK((Logger::getLevel() == spdlog::level::critical));

			Logger::setLevel(spdlog::level::info);
		}
	}

	GIVEN("setLoggerName")
	{
		THEN("works after renaming")
		{
			bool result{Logger::setLoggerName("renamed_logger")};
			REQUIRE(result);

			std::optional<std::string_view> infoResult{Logger::info("after rename")};
			CHECK_FALSE(infoResult.has_value());

			result = Logger::setLoggerName(loggerName);

			CHECK(result);
		}
	}

	GIVEN("setFileName")
	{
		THEN("works after file name changed")
		{
			std::string newFile{"logger_test_newfile.log"};
			fileRemoved = std::filesystem::remove(newFile);

			REQUIRE(!fileRemoved);

			const bool setResult{Logger::setFileName(newFile)};
			CHECK(setResult);

			std::optional<std::string_view> result{Logger::info("written to new file")};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile(&newFile)};
			CHECK(contents.contains("written to new file"));

			spdlog::drop_all();
			fileRemoved = std::filesystem::remove(newFile);
			REQUIRE(fileRemoved);

			loggerInitialized = Logger::initialize(loggerName, logFileName);
			REQUIRE(loggerInitialized);
		}
	}

	GIVEN("setLoggerAndFileName")
	{
		THEN("works with both name and file changed")
		{
			std::string newFile{"logger_test_both.log"};
			fileRemoved = std::filesystem::remove(newFile);
			REQUIRE(!fileRemoved);

			const bool setResult{Logger::setLoggerAndFileName("both_logger", newFile)};
			CHECK(setResult);

			std::optional<std::string_view> result{Logger::info("both changed")};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile(&newFile)};
			CHECK(contents.contains("both changed"));

			spdlog::drop_all();
			fileRemoved = std::filesystem::remove(newFile);
			REQUIRE(fileRemoved);

			loggerInitialized = Logger::initialize(loggerName, logFileName);
			REQUIRE(loggerInitialized);
		}
	}

	GIVEN("Template logging methods")
	{
		THEN("specifying logging level writes to file")
		{
			std::optional<std::string_view> result{Logger::log(spdlog::level::info, "log message {}", 77)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("log message 77"));
		}

		THEN("trace writes message to log")
		{
			Logger::setLevel(spdlog::level::trace);

			std::optional<std::string_view> result{Logger::trace("trace message {}", 250)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("trace message 250"));

			Logger::setLevel(spdlog::level::info);
		}

		THEN("debug writes message to log")
		{
			Logger::setLevel(spdlog::level::debug);

			std::optional<std::string_view> result{Logger::debug("debug message {}", 489)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("debug message 489"));

			Logger::setLevel(spdlog::level::info);
		}

		THEN("info writes message to log")
		{
			std::optional<std::string_view> result{Logger::info("info message {}", 100)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("info message 100"));
		}

		THEN("warn writes message to log")
		{
			std::optional<std::string_view> result{Logger::warn("warn message {}", 3.14)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("warn message 3.14"));
		}

		THEN("error writes message to log")
		{
			std::optional<std::string_view> result{Logger::error("error message {}", "failure")};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("error message failure"));
		}

		THEN("critical writes message to log")
		{
			std::optional<std::string_view> result{Logger::critical("critical message {}", 999)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("critical message 999"));
		}
	}

	GIVEN("Level filtering")
	{
		THEN("info level messages are suppressed when level is set to error")
		{
			Logger::setLevel(spdlog::level::err);
			std::optional<std::string_view> result{Logger::info("should not appear")};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK_FALSE(contents.contains("should not appear"));

			Logger::setLevel(spdlog::level::info);
		}

		THEN("warn level messages are suppressed when level is set to critical")
		{
			Logger::setLevel(spdlog::level::critical);
			std::optional<std::string_view> result{Logger::warn("suppressed warning")};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK_FALSE(contents.contains("suppressed warning"));

			Logger::setLevel(spdlog::level::info);
		}
	}

	GIVEN("Multiple messages at info level")
	{
		THEN("all messages accumulate in the log file")
		{
			std::optional<std::string_view> result1{Logger::info("first message")};
			std::optional<std::string_view> result2{Logger::info("second message")};
			std::optional<std::string_view> result3{Logger::info("third message")};
			CHECK_FALSE(result1.has_value());
			CHECK_FALSE(result2.has_value());
			CHECK_FALSE(result3.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("first message"));
			CHECK(contents.contains("second message"));
			CHECK(contents.contains("third message"));
		}
	}

	GIVEN("Template instantiations with string_view arguments")
	{
		THEN("info with single string_view argument")
		{
			const std::string_view arg{"type_name"};
			std::optional<std::string_view> result{Logger::info("registered {}", arg)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("registered type_name"));
		}

		THEN("info with two string_view arguments")
		{
			const std::string_view arg1{"fire"};
			const std::string_view arg2{"water"};
			std::optional<std::string_view> result{Logger::info("{} vs {}", arg1, arg2)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("fire vs water"));
		}

		THEN("warn with single string_view argument")
		{
			const std::string_view arg{"warning_detail"};
			std::optional<std::string_view> result{Logger::warn("issue: {}", arg)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("issue: warning_detail"));
		}

		THEN("warn with two string_view arguments")
		{
			const std::string_view arg1{"expected"};
			const std::string_view arg2{"actual"};
			std::optional<std::string_view> result{Logger::warn("{} != {}", arg1, arg2)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("expected != actual"));
		}
	}

	GIVEN("Template instantiations with unsigned char arguments")
	{
		THEN("info with single unsigned char argument")
		{
			const ub arg{5};
			std::optional<std::string_view> result{Logger::info("type id {}", arg)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("type id 5"));
		}

		THEN("warn with unsigned char and string_view arguments")
		{
			const ub userID{3};
			const std::string_view name{"grass"};
			std::optional<std::string_view> result{Logger::warn("id {} name {}", userID, name)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("id 3 name grass"));
		}

		THEN("warn with two unsigned char arguments")
		{
			const ub id1{1};
			const ub id2{2};
			std::optional<std::string_view> result{Logger::warn("ids {} {}", id1, id2)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("ids 1 2"));
		}

		THEN("warn with unsigned long and unsigned char arguments")
		{
			const ub userID{7};
			std::optional<std::string_view> result{Logger::warn("size {} id {}", 20UL, userID)};
			CHECK_FALSE(result.has_value());

			const std::string contents{readLogFile()};
			CHECK(contents.contains("size 20 id 7"));
		}
	}

	GIVEN("Failure coverage for template instantiations with spdlog_ex")
	{
		THEN("info with single string_view argument throws spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });
			const std::string_view arg{"x"};

			std::optional<std::string_view> result{Logger::info("{} {}", arg)};
			REQUIRE(result.has_value());

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("info with two string_view arguments throws spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });
			const std::string_view arg1{"a"};
			const std::string_view arg2{"b"};

			std::optional<std::string_view> result{Logger::info("{} {} {}", arg1, arg2)};
			REQUIRE(result.has_value());

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("info with single unsigned char argument throws spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });
			const ub arg{1};

			std::optional<std::string_view> result{Logger::info("{} {}", arg)};
			REQUIRE(result.has_value());

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("warn with single string_view argument throws spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });
			const std::string_view arg{"x"};

			std::optional<std::string_view> result{Logger::warn("{} {}", arg)};
			REQUIRE(result.has_value());

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("warn with two string_view arguments throws spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });
			const std::string_view arg1{"a"};
			const std::string_view arg2{"b"};

			std::optional<std::string_view> result{Logger::warn("{} {} {}", arg1, arg2)};
			REQUIRE(result.has_value());

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("warn with unsigned char and string_view arguments throws spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });
			const ub userID{1};
			const std::string_view name{"x"};

			std::optional<std::string_view> result{Logger::warn("{} {} {}", userID, name)};
			REQUIRE(result.has_value());

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("warn with two unsigned char arguments throws spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });
			const ub id1{1};
			const ub id2{2};

			std::optional<std::string_view> result{Logger::warn("{} {} {}", id1, id2)};
			REQUIRE(result.has_value());

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("warn with unsigned long and unsigned char arguments throws spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });
			const ub userID{1};

			std::optional<std::string_view> result{Logger::warn("{} {} {}", 20UL, userID)};
			REQUIRE(result.has_value());

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}
	}

	GIVEN("Non-spdlog exception handling for template instantiations")
	{
		THEN("info with single string_view argument propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });
			const std::string_view arg{"x"};

			CHECK_THROWS_AS(static_cast<void>(Logger::info("{} {}", arg)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("info with two string_view arguments propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });
			const std::string_view arg1{"a"};
			const std::string_view arg2{"b"};

			CHECK_THROWS_AS(static_cast<void>(Logger::info("{} {} {}", arg1, arg2)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("info with single unsigned char argument propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });
			const ub arg{1};

			CHECK_THROWS_AS(static_cast<void>(Logger::info("{} {}", arg)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("warn with single string_view argument propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });
			const std::string_view arg{"x"};

			CHECK_THROWS_AS(static_cast<void>(Logger::warn("{} {}", arg)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("warn with two string_view arguments propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });
			const std::string_view arg1{"a"};
			const std::string_view arg2{"b"};

			CHECK_THROWS_AS(static_cast<void>(Logger::warn("{} {} {}", arg1, arg2)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("warn with unsigned char and string_view arguments propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });
			const ub userID{1};
			const std::string_view name{"x"};

			CHECK_THROWS_AS(static_cast<void>(Logger::warn("{} {} {}", userID, name)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("warn with two unsigned char arguments propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });
			const ub id1{1};
			const ub id2{2};

			CHECK_THROWS_AS(static_cast<void>(Logger::warn("{} {} {}", id1, id2)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("warn with unsigned long and unsigned char arguments propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });
			const ub userID{1};

			CHECK_THROWS_AS(static_cast<void>(Logger::warn("{} {} {}", 20UL, userID)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}
	}

	GIVEN("truncateFile parameter")
	{
		THEN("existing content is cleared when initialized with truncate")
		{
			// Write some content through the logger so the file is non-empty
			std::optional<std::string_view> preResult{Logger::info("pre-existing content")};
			CHECK_FALSE(preResult.has_value());

			spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });

			std::string contents{readLogFile()};
			REQUIRE_FALSE(contents.empty());

			// Re-initialize with truncateFile true
			spdlog::drop_all();
			loggerInitialized = Logger::initialize(loggerName, logFileName, true);
			REQUIRE(loggerInitialized);

			std::optional<std::string_view> postResult{Logger::info("after truncate")};
			CHECK_FALSE(postResult.has_value());
			contents = readLogFile();

			CHECK_FALSE(contents.contains("pre-existing content"));
			CHECK(contents.contains("after truncate"));
		}

		THEN("existing content is preserved when initialized without truncate")
		{
			std::optional<std::string_view> keepResult{Logger::info("keep this content")};
			CHECK_FALSE(keepResult.has_value());

			spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });

			// Re-initialize without truncation (default)
			spdlog::drop_all();
			loggerInitialized = Logger::initialize("preserve_logger", logFileName, false);
			REQUIRE(loggerInitialized);

			std::optional<std::string_view> appendResult{Logger::info("appended content")};
			CHECK_FALSE(appendResult.has_value());
			spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) { logger->flush(); });

			std::string contents{readLogFile()};
			CHECK(contents.contains("keep this content"));
			CHECK(contents.contains("appended content"));
		}

		THEN("initialization fails for invalid path with truncate")
		{
			spdlog::drop_all();

			// A path under a non-existent directory should fail to open for truncation
			loggerInitialized = Logger::initialize("trunc_fail_logger", "/no_such_dir/no_such_subdir/fail.log", true);
			CHECK_FALSE(loggerInitialized);
		}
	}

	GIVEN("spdlog initialization with duplicate registry name")
	{
		THEN("initialization fails when duplicate name exists")
		{
			// Manually register a logger with a different name so spdlog::basic_logger_mt throws
			std::string duplicateName{"duplicate_logger"};
			std::string tempFile{"logger_test_dup.log"};
			const std::shared_ptr<spdlog::logger> manualLogger{spdlog::basic_logger_mt(duplicateName, tempFile)};

			// initialize will reset its own internal state but cannot drop the manually registered logger,
			// causing spdlog to throw spdlog_ex which is caught internally and returns false.
			loggerInitialized = Logger::initialize(duplicateName, logFileName);
			CHECK_FALSE(loggerInitialized);

			spdlog::drop(duplicateName);
			fileRemoved = std::filesystem::remove(tempFile);

			REQUIRE(fileRemoved);
		}
	}

	GIVEN("Logging failure returning error string_view on spdlog_ex")
	{
		THEN("log returns error string when spdlog_ex is thrown")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });

			std::optional<std::string_view> result{Logger::log(spdlog::level::info, "{} {}", 77)};

			REQUIRE(result.has_value());
			CHECK((result.value() == Logging::LOG_LOG_FAILURE));

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("trace returns error string when spdlog_ex is thrown")
		{
			Logger::setLevel(spdlog::level::trace);
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });

			std::optional<std::string_view> result{Logger::trace("{} {}", 42)};

			REQUIRE(result.has_value());
			CHECK((result.value() == Logging::TRACE_LOG_FAILURE));

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
			Logger::setLevel(spdlog::level::info);
		}

		THEN("debug returns error string when spdlog_ex is thrown")
		{
			Logger::setLevel(spdlog::level::debug);
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });

			std::optional<std::string_view> result{Logger::debug("{} {}", "hello")};

			REQUIRE(result.has_value());
			CHECK((result.value() == Logging::DEBUG_LOG_FAILURE));

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
			Logger::setLevel(spdlog::level::info);
		}

		THEN("info returns error string when spdlog_ex is thrown")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });

			std::optional<std::string_view> result{Logger::info("{} {}", 100)};

			REQUIRE(result.has_value());
			CHECK((result.value() == Logging::INFO_LOG_FAILURE));

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("warn returns error string when spdlog_ex is thrown")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });

			std::optional<std::string_view> result{Logger::warn("{} {}", 3.14)};

			REQUIRE(result.has_value());
			CHECK((result.value() == Logging::WARN_LOG_FAILURE));

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("error returns error string when spdlog_ex is thrown")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });

			std::optional<std::string_view> result{Logger::error("{} {}", "failure")};

			REQUIRE(result.has_value());
			CHECK((result.value() == Logging::ERROR_LOG_FAILURE));

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("critical returns error string when spdlog_ex is thrown")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw spdlog::spdlog_ex(msg); });

			std::optional<std::string_view> result{Logger::critical("{} {}", 999)};

			REQUIRE(result.has_value());
			CHECK((result.value() == Logging::CRITICAL_LOG_FAILURE));

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}
	}

	GIVEN("Logging failure propagating non-spdlog_ex")
	{
		THEN("log propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });

			CHECK_THROWS_AS(static_cast<void>(Logger::log(spdlog::level::info, "{} {}", 77)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("trace propagates non-spdlog_ex")
		{
			Logger::setLevel(spdlog::level::trace);
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });

			CHECK_THROWS_AS(static_cast<void>(Logger::trace("{} {}", 42)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
			Logger::setLevel(spdlog::level::info);
		}

		THEN("debug propagates non-spdlog_ex")
		{
			Logger::setLevel(spdlog::level::debug);
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });

			CHECK_THROWS_AS(static_cast<void>(Logger::debug("{} {}", "hello")), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
			Logger::setLevel(spdlog::level::info);
		}

		THEN("info propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });

			CHECK_THROWS_AS(static_cast<void>(Logger::info("{} {}", 100)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("warn propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });

			CHECK_THROWS_AS(static_cast<void>(Logger::warn("{} {}", 3.14)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("error propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });

			CHECK_THROWS_AS(static_cast<void>(Logger::error("{} {}", "failure")), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}

		THEN("critical propagates non-spdlog_ex")
		{
			spdlog::set_error_handler([] ATTR_NORETURN(const std::string &msg) { throw std::runtime_error(msg); });

			CHECK_THROWS_AS(static_cast<void>(Logger::critical("{} {}", 999)), std::runtime_error);

			spdlog::set_error_handler([](const std::string & /*msg*/) {});
		}
	}

	// Scenario-level cleanup
	spdlog::drop_all();
	fileRemoved = std::filesystem::remove(logFileName);

	REQUIRE(fileRemoved);
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)
