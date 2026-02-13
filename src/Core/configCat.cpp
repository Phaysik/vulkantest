/*! \file configCat.cpp
	\brief Contains the function definitions for creating a configCat
	\date 02/12/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#include "Core/configCat.h"

#include <memory>
#include <string>

#include <configcat/configcatclient.h>
#include <configcat/consolelogger.h>
#include <configcat/log.h>

namespace Dimensia::Core
{
	ConfigCat::ConfigCat(const std::string &sdkKey) noexcept
	{
		options().logger = std::make_shared<configcat::ConsoleLogger>(configcat::LogLevel::LOG_LEVEL_DEBUG);
		client() = configcat::ConfigCatClient::get(sdkKey, &options());
	}
} // namespace Dimensia::Core