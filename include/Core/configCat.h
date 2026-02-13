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
	class ConfigCat
	{

		public:
			explicit ConfigCat() = default;
			explicit ConfigCat(const std::string &sdkKey) noexcept;

			static void setSDKKey(const std::string &sdkKey)
			{
				client() = configcat::ConfigCatClient::get(sdkKey, &options());
			}

			static void closeClient()
			{
				configcat::ConfigCatClient::close(client());
				client().reset();
			}

			static bool getValue(const std::string &key, const bool defaultValue = false)
			{
				if (client())
				{
					return client()->getValue(key, defaultValue);
				}

				return defaultValue;
			}

		private:
			static configcat::ConfigCatOptions &options()
			{
				static configcat::ConfigCatOptions options{};
				return options;
			}

			static std::shared_ptr<configcat::ConfigCatClient> &client()
			{
				static std::shared_ptr<configcat::ConfigCatClient> client = nullptr;
				return client;
			}
	};
} // namespace Dimensia::Core

#endif