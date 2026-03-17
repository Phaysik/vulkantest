/*! \file configCat.h
	\brief Contains the function declarations for creating initializing ConfigCat
	\date 02/12/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#ifndef INCLUDE_CORE_CONFIGCAT_H
#define INCLUDE_CORE_CONFIGCAT_H

#include <memory>
#include <string>

#include <configcat/configcatclient.h>
#include <configcat/configcatoptions.h>

namespace Dimensia::Core
{
	/*! @class ConfigCat Core/configCat.h
		@brief Wrapper for a process-global ConfigCat SDK client used for feature flag lookups.
		@details `ConfigCat` provides a thin, process-scoped facade around the ConfigCat C++ SDK. It exposes static helpers to initialise
	   the shared `configcat::ConfigCatClient` using an SDK key (`setSDKKey`), to close the client (`closeClient`), and to read boolean
	   feature flags (`getValue`). The implementation holds a `static` `configcat::ConfigCatOptions` and a `static`
	   `std::shared_ptr<configcat::ConfigCatClient>` returned from the private helpers `options()` and `client()` respectively. These
	   singletons have static lifetime and are used by all callers in the process.
		@note Concurrent initialization / close is not synchronized by this helper. Caller-side synchronization is required if multiple
	   threads may call `setSDKKey` / `closeClient` concurrently.
	*/
	class ConfigCat
	{
		public:
			// MARK: Constructors

			/*! @brief Default-construct a `ConfigCat` helper object.
				@details This constructor is trivial and performs no SDK initialization. To initialize the process-global ConfigCat client,
			   either use the `ConfigCat(const std::string &sdkKey)` constructor or call `setSDKKey()` after construction. The default
			   constructor is intended for use when a lightweight helper instance is required without side effects.
			*/
			explicit ConfigCat() = default;

			/*! @brief Construct and initialize the shared ConfigCat client.
				@details Initializes the process-global `configcat::ConfigCatClient` using the provided @p sdkKey and the singleton options
			   object. This constructor is `noexcept` in the implementation.
				@param[in] sdkKey The ConfigCat SDK key used to create the client.
				@post After construction, `client()` will hold a valid shared pointer if creation succeeded.
			*/
			explicit ConfigCat(const std::string &sdkKey) noexcept;

			// MARK: Static Getter

			/*! @brief Retrieve a boolean feature flag value.
				@details If the global SDK client is initialized, forwards to `configcat::ConfigCatClient::getValue(key, defaultValue)`. If
			   the client is not initialized, returns false.
				@param[in] key The feature flag key to query.
				@param[in] defaultValue Value returned when the flag is unset.
				@return The flag value when available; otherwise false.
			*/
			static bool getValue(const std::string &key, const bool defaultValue = false)
			{
				if (client())
				{
					return client()->getValue(key, defaultValue);
				}

				return false;
			}

			// MARK: Static Setter

			/*! @brief Initialize or replace the process-global ConfigCat client.
				@details Calls `configcat::ConfigCatClient::get(sdkKey, &options())` and stores the result in the process-global client
			   instance. If a client already exists, it will be replaced by the newly returned shared_ptr.
				@param[in] sdkKey The SDK key used to obtain a client.
				@note No synchronization is performed; callers must ensure thread-safety when invoking this concurrently.
			*/
			static void setSDKKey(const std::string &sdkKey)
			{
				client() = configcat::ConfigCatClient::get(sdkKey, &options());
			}

			// MARK: Static Member Function

			/*! @brief Close and reset the process-global client.
				@details Calls `configcat::ConfigCatClient::close(client())` to perform any SDK shutdown, then resets the stored shared_ptr.
			   After this call `client()` will be empty.
			*/
			static void closeClient()
			{
				configcat::ConfigCatClient::close(client());
				client().reset();
			}

		private:
			// MARK: Private Static Member Functions

			/*! @brief Access the process-global `ConfigCatOptions` instance.
				@details The options object is created on first use and lives for the lifetime of the process. Use this to configure
			   SDK-level options prior to initializing the client.
				@return Reference to the singleton `configcat::ConfigCatOptions`.
			*/
			static configcat::ConfigCatOptions &options()
			{
				static configcat::ConfigCatOptions options{};
				return options;
			}

			/*! @brief Access the process-global shared pointer to the SDK client.
				@details The returned reference refers to a `static` local `std::shared_ptr<configcat::ConfigCatClient>`. It may be empty if
			   the client has not been initialized (see `setSDKKey`).
				@return Reference to the singleton `std::shared_ptr<configcat::ConfigCatClient>`.
			*/
			static std::shared_ptr<configcat::ConfigCatClient> &client()
			{
				static std::shared_ptr<configcat::ConfigCatClient> client = nullptr;
				return client;
			}
	};
} // namespace Dimensia::Core

#endif