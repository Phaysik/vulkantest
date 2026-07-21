/*! \file logger.cpp
	\brief Contains the function definitions for creating a logger
	@date 02/12/2026
	@version 0.0.1
	@since 0.0.1
	\author Matthew Moore
*/

#include "Utility/Debug/Logging/logger.h"

#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include "Core/attributeMacros.h"

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace Dimensia::Utility::Debug::Logging
{
	// MARK: Getter

	ATTR_NODISCARD spdlog::level::level_enum Logger::getLevel()
	{
		return getLoggerInstance()->level();
	}

	// MARK: Setters

	void Logger::setLevel(spdlog::level::level_enum level)
	{
		getLoggerInstance()->set_level(level);
	}

	ATTR_NODISCARD bool Logger::setLoggerName(const std::string &loggerName)
	{
		return initialize(loggerName, getFileNameStore());
	}

	ATTR_NODISCARD bool Logger::setFileName(const std::string &fileName)
	{
		return initialize(getLoggerName(), fileName);
	}

	ATTR_NODISCARD bool Logger::setLoggerAndFileName(const std::string &loggerName, const std::string &fileName)
	{
		return initialize(loggerName, fileName);
	}

	// MARK: Static Member Function

	bool Logger::initialize(std::string_view loggerName, std::string_view fileName, const bool truncateFile)
	{
		const std::string convertedLoggerName{loggerName}; // LCOV_EXCL_BR_LINE — uncovered branch is the compiler-generated throw edge from
														   // std::string construction (std::bad_alloc)
		const std::string convertedFileName{fileName};	   // LCOV_EXCL_BR_LINE — uncovered branch is the compiler-generated throw edge from
														   // std::string construction (std::bad_alloc)

		if (getLoggerInstance()) // LCOV_EXCL_BR_LINE — uncovered branch is the compiler-generated throw edge from shared_ptr bool
								 // conversion
		{
			spdlog::drop(getLoggerName()); // LCOV_EXCL_BR_LINE — uncovered branches are compiler-generated throw edges from getLoggerName()
										   // and spdlog::drop()
		}

		getLoggerInstance().reset(); // LCOV_EXCL_BR_LINE — uncovered branch is the compiler-generated throw edge from the accessor call

		getLoggerName() = convertedLoggerName;	// LCOV_EXCL_BR_LINE — uncovered branches are compiler-generated throw edges from accessor
												// call and string assignment
		getFileNameStore() = convertedFileName; // LCOV_EXCL_BR_LINE — uncovered branches are compiler-generated throw edges from accessor
												// call and string assignment

		if (truncateFile)
		{
			// LCOV_EXCL_BR_START — uncovered branch is the compiler-generated throw edge from ofstream construction
			std::ofstream ofs(convertedFileName, std::ofstream::out | std::ofstream::trunc);
			// LCOV_EXCL_BR_STOP

			if (!ofs.is_open())
			{
				return false;
			}
		}

		try
		{
			// LCOV_EXCL_BR_START — uncovered branch is the compiler-generated throw edge from shared_ptr assignment
			getLoggerInstance() = spdlog::basic_logger_mt(convertedLoggerName, convertedFileName);
			// LCOV_EXCL_BR_STOP
		}
		// LCOV_EXCL_BR_START — uncovered branch is the catch-clause type-mismatch fallthrough; only reachable if a non-spdlog_ex escapes
		// basic_logger_mt (e.g. std::bad_alloc)
		catch (const spdlog::spdlog_ex &ex)
		{
			return false;
		}
		// LCOV_EXCL_BR_STOP

		return true;
	}

// GCC incorrectly suggests returns_nonnull for reference-returning functions; suppress since references are inherently non-null.
#if defined(ATTR_GCC) && !defined(ATTR_CLANG)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wsuggest-attribute=returns_nonnull"
#endif

	std::shared_ptr<spdlog::logger> &Logger::getLoggerInstance()
	{
		static std::shared_ptr<spdlog::logger>
			logger; // LCOV_EXCL_BR_LINE — fourth branch is the __cxa_atexit destructor-registration failure path, only reachable on OOM
		return logger;
	}

	std::string &Logger::getLoggerName()
	{
		static std::string loggerName; // LCOV_EXCL_BR_LINE — fourth branch is the __cxa_atexit destructor-registration failure path,
									   // only reachable on OOM
		return loggerName;
	}

	std::string &Logger::getFileNameStore()
	{
		static std::string fileName; // LCOV_EXCL_BR_LINE — fourth branch is the __cxa_atexit destructor-registration failure path, only
									 // reachable on OOM
		return fileName;
	}

#if defined(ATTR_GCC) && !defined(ATTR_CLANG)
	#pragma GCC diagnostic pop
#endif
} // namespace Dimensia::Utility::Debug::Logging