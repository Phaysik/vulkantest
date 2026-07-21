/*! @file logger.h
	@brief Contains the function declarations for a static logging wrapper around spdlog.
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_UTILITY_DEBUG_LOGGING_LOGGER_H
#define INCLUDE_UTILITY_DEBUG_LOGGING_LOGGER_H

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "Core/attributeMacros.h"
#include "Utility/Debug/Logging/constants.h"

#include <spdlog/logger.h>

/*! @namespace Dimensia::Utility::Debug::Logging Provides debug and diagnostic logging facilities.
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/
namespace Dimensia::Utility::Debug::Logging
{
	/*! @class Logger logger.h "include/Utility/Debug/Logging/logger.h"
		@brief A static-only wrapper around spdlog that provides global logging through deferred initialization.
		@details All constructors, copy/move operators, and the destructor are deleted to prevent instantiation. Call @ref initialize before
	   any logging methods. Internal state is stored via function-local statics to avoid static-initialization-order issues.
	*/
	class Logger
	{
		public:
			/*! @brief Default constructor is deleted to prevent instantiation. */
			Logger() = delete ("Logger is not instantiable");

			/*! @brief Copy constructor is deleted to prevent instantiation. */
			Logger(const Logger &) = delete ("Logger is not copyable");

			/*! @brief Move constructor is deleted to prevent instantiation. */
			Logger(Logger &&) = delete ("Logger is not movable");

			/*! @brief Copy assignment operator is deleted to prevent instantiation. */
			Logger &operator=(const Logger &) = delete ("Logger is not copyable");

			/*! @brief Move assignment operator is deleted to prevent instantiation. */
			Logger &operator=(Logger &&) = delete ("Logger is not movable");

			/*! @brief Destructor is deleted to prevent instantiation. */
			~Logger() = delete ("Logger is not instantiable");

			// MARK: Getter

			/*! @brief Gets the current logging level of the underlying spdlog logger.
				@pre @ref initialize must have been called before invoking this method.
				@return The current spdlog logging level.
			*/
			ATTR_NODISCARD static spdlog::level::level_enum getLevel();

			// MARK: Setters

			/*! @brief Sets the logging level of the underlying spdlog logger.
				@pre @ref initialize must have been called before invoking this method.
				@param[in] level The spdlog level to set (e.g., spdlog::level::debug).
			*/
			static void setLevel(spdlog::level::level_enum level);

			/*! @brief Replaces the logger with a new one using the given name, keeping the current file.
				@details Destroys the existing spdlog logger and creates a new one via @ref initializeLogger.
				@pre @ref initialize must have been called before invoking this method.
				@post The internal logger is replaced; the previous logger is destroyed.
				@param[in] loggerName The new name for the logger.
				@throws std::runtime_error If spdlog re-initialization fails.
			*/
			ATTR_NODISCARD static bool setLoggerName(const std::string &loggerName);

			/*! @brief Replaces the logger with a new one writing to the given file, keeping the current name.
				@details Destroys the existing spdlog logger and creates a new one via @ref initializeLogger.
				@pre @ref initialize must have been called before invoking this method.
				@post The internal logger is replaced; the previous logger is destroyed.
				@param[in] fileName The new file path for log output.
				@throws std::runtime_error If spdlog re-initialization fails.
			*/
			ATTR_NODISCARD static bool setFileName(const std::string &fileName);

			/*! @brief Replaces the logger with a new one using the given name and file, keeping the current settings.
				@details Destroys the existing spdlog logger and creates a new one via @ref initializeLogger.
				@pre @ref initialize must have been called before invoking this method.
				@post The internal logger is replaced; the previous logger is destroyed.
				@param[in] loggerName The new name for the logger.
				@param[in] fileName The new file path for log output.
				@throws std::runtime_error If spdlog re-initialization fails.
			*/
			ATTR_NODISCARD static bool setLoggerAndFileName(const std::string &loggerName, const std::string &fileName);

			// MARK: Static Member Function

			/*! @brief Initializes the static logger with the given name and output file.
				@details Creates a new spdlog file logger via spdlog::basic_logger_mt and stores it internally. Must be called before any
			   logging methods (trace, debug, info, etc.).
				@param[in] loggerName The name used to identify the logger within spdlog's registry.
				@param[in] fileName The path to the log output file.
				@param[in] truncateFile If true, the file at `fileName` will be truncated (cleared) before the logger is created. Defaults
			   to false.
				@throws std::runtime_error If spdlog initialization or file truncation fails.
			*/
			ATTR_NODISCARD static bool initialize(std::string_view loggerName, std::string_view fileName, const bool truncateFile = false);

			// MARK: Static Template Member Functions

			/*! @brief Logs a message at the specified level.
				@pre @ref initialize must have been called before invoking this method.
				@tparam Args The types of the format arguments.
				@param[in] format The fmt-style format string.
				@param[in] args The arguments to format into the message.
				@param[in] level The spdlog level to log at (defaults to spdlog::level::info).
			*/
			template <typename... Args>
			ATTR_NODISCARD static std::optional<std::string_view> log(spdlog::level::level_enum level, const std::string_view &format,
																	  Args &&...args)
			{
				const std::shared_ptr<spdlog::logger> &logger = getLoggerInstance();

				try
				{
					logger->log(level, fmt::runtime(format), std::forward<Args>(args)...);
				}
				catch (const spdlog::spdlog_ex &ex)
				{
					return LOG_LOG_FAILURE;
				}

				return std::nullopt;
			}

			/*! @brief Logs a message at the trace level.
				@pre @ref initialize must have been called before invoking this method.
				@tparam Args The types of the format arguments.
				@param[in] format The fmt-style format string.
				@param[in] args The arguments to format into the message.
			*/
			template <typename... Args>
			ATTR_NODISCARD static std::optional<std::string_view> trace(const std::string_view &format, Args &&...args)
			{
				const std::shared_ptr<spdlog::logger> &logger = getLoggerInstance();

				try
				{
					logger->trace(fmt::runtime(format), std::forward<Args>(args)...);
				}
				catch (const spdlog::spdlog_ex &ex)
				{
					return TRACE_LOG_FAILURE;
				}

				return std::nullopt;
			}

			/*! @brief Logs a message at the debug level.
				@pre @ref initialize must have been called before invoking this method.
				@tparam Args The types of the format arguments.
				@param[in] format The fmt-style format string.
				@param[in] args The arguments to format into the message.
			*/
			template <typename... Args>
			ATTR_NODISCARD static std::optional<std::string_view> debug(const std::string_view &format, Args &&...args)
			{
				const std::shared_ptr<spdlog::logger> &logger = getLoggerInstance();

				try
				{
					logger->debug(fmt::runtime(format), std::forward<Args>(args)...);
				}
				catch (const spdlog::spdlog_ex &ex)
				{
					return DEBUG_LOG_FAILURE;
				}

				return std::nullopt;
			}

			/*! @brief Logs a message at the info level.
				@pre @ref initialize must have been called before invoking this method.
				@tparam Args The types of the format arguments.
				@param[in] format The fmt-style format string.
				@param[in] args The arguments to format into the message.
			*/
			template <typename... Args>
			ATTR_NODISCARD static std::optional<std::string_view> info(const std::string_view &format, Args &&...args)
			{
				const std::shared_ptr<spdlog::logger> &logger = getLoggerInstance();

				try
				{
					logger->info(fmt::runtime(format), std::forward<Args>(args)...);
				}
				catch (const spdlog::spdlog_ex &ex)
				{
					return INFO_LOG_FAILURE;
				}

				return std::nullopt;
			}

			/*! @brief Logs a message at the warn level.
				@pre @ref initialize must have been called before invoking this method.
				@tparam Args The types of the format arguments.
				@param[in] format The fmt-style format string.
				@param[in] args The arguments to format into the message.
			*/
			template <typename... Args>
			ATTR_NODISCARD static std::optional<std::string_view> warn(const std::string_view &format, Args &&...args)
			{
				const std::shared_ptr<spdlog::logger> &logger = getLoggerInstance();

				try
				{
					logger->warn(fmt::runtime(format), std::forward<Args>(args)...);
				}
				catch (const spdlog::spdlog_ex &ex)
				{
					return WARN_LOG_FAILURE;
				}

				return std::nullopt;
			}

			/*! @brief Logs a message at the error level.
				@pre @ref initialize must have been called before invoking this method.
				@tparam Args The types of the format arguments.
				@param[in] format The fmt-style format string.
				@param[in] args The arguments to format into the message.
			*/
			template <typename... Args>
			ATTR_NODISCARD static std::optional<std::string_view> error(const std::string_view &format, Args &&...args)
			{
				const std::shared_ptr<spdlog::logger> &logger = getLoggerInstance();

				try
				{
					logger->error(fmt::runtime(format), std::forward<Args>(args)...);
				}
				catch (const spdlog::spdlog_ex &ex)
				{
					return ERROR_LOG_FAILURE;
				}

				return std::nullopt;
			}

			/*! @brief Logs a message at the critical level.
				@pre @ref initialize must have been called before invoking this method.
				@tparam Args The types of the format arguments.
				@param[in] format The fmt-style format string.
				@param[in] args The arguments to format into the message.
			*/
			template <typename... Args>
			ATTR_NODISCARD static std::optional<std::string_view> critical(const std::string_view &format, Args &&...args)
			{
				const std::shared_ptr<spdlog::logger> &logger = getLoggerInstance();

				try
				{
					logger->critical(fmt::runtime(format), std::forward<Args>(args)...);
				}
				catch (const spdlog::spdlog_ex &ex)
				{
					return CRITICAL_LOG_FAILURE;
				}

				return std::nullopt;
			}

		private:
			// MARK: Private Static Member Functions

			/*! @brief Provides access to the function-local static spdlog logger instance.
				@return A reference to the shared pointer holding the spdlog logger. The reference remains valid for the lifetime of the
			   program.
			*/
			static std::shared_ptr<spdlog::logger> &getLoggerInstance();

			/*! @brief Provides access to the function-local static logger name string.
				@return A reference to the stored logger name. The reference remains valid for the lifetime of the program.
			*/
			static std::string &getLoggerName();

			/*! @brief Provides access to the function-local static file name string.
				@return A reference to the stored file name. The reference remains valid for the lifetime of the program.
			*/
			static std::string &getFileNameStore();
	};
} // namespace Dimensia::Utility::Debug::Logging

#endif