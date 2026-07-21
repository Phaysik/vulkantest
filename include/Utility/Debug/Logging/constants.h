/*! @file constants.h
	@brief Contains the constant definitions for use with logging.
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_UTILITY_DEBUG_LOGGING_CONSTANTS_H
#define INCLUDE_UTILITY_DEBUG_LOGGING_CONSTANTS_H

#include <string_view>

namespace Dimensia::Utility::Debug::Logging
{
	/*! @brief The default logger name used for the static Logger wrapper. */
	constexpr std::string_view LOGGING_LOGGER_NAME{"project_logger"};

	/*! @brief The default log file name used for the static Logger wrapper. */
	constexpr std::string_view LOGGING_FILE_NAME{"project.log"};

	/*! @brief Error message returned when a call to @ref Logger::log fails. */
	constexpr std::string_view LOG_LOG_FAILURE{
		"Failed to log the log message. This likely indicates a severe issue with the logging system itself."};

	/*! @brief Error message returned when a call to @ref Logger::trace fails. */
	constexpr std::string_view TRACE_LOG_FAILURE{
		"Failed to log the trace message. This likely indicates a severe issue with the logging system itself."};

	/*! @brief Error message returned when a call to @ref Logger::debug fails. */
	constexpr std::string_view DEBUG_LOG_FAILURE{
		"Failed to log the debug message. This likely indicates a severe issue with the logging system itself."};

	/*! @brief Error message returned when a call to @ref Logger::info fails. */
	constexpr std::string_view INFO_LOG_FAILURE{
		"Failed to log the info message. This likely indicates a severe issue with the logging system itself."};

	/*! @brief Error message returned when a call to @ref Logger::warn fails. */
	constexpr std::string_view WARN_LOG_FAILURE{
		"Failed to log the warning message. This likely indicates a severe issue with the logging system itself."};

	/*! @brief Error message returned when a call to @ref Logger::error fails. */
	constexpr std::string_view ERROR_LOG_FAILURE{
		"Failed to log the error message. This likely indicates a severe issue with the logging system itself."};

	/*! @brief Error message returned when a call to @ref Logger::critical fails. */
	constexpr std::string_view CRITICAL_LOG_FAILURE{
		"Failed to log the critical message. This likely indicates a severe issue with the logging system itself."};
} // namespace Dimensia::Utility::Debug::Logging

#endif