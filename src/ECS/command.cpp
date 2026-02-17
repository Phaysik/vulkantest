/*! \file command.cpp
	\brief Contains the function definitions for creating a command
	\date 02/17/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#include "ECS/command.h"

#include <cassert>

#include "ECS/componentRegistry.h"

namespace Dimensia::ECS
{
	// MARK: Member Function

	void Command::destroyBuffer()
	{
		if (mType == CmdType::AddComponent)
		{
			auto &addData = std::get<AddData>(mData);

			assert(addData.compId < Dimensia::Registry::ComponentInfos.size());

			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			Dimensia::Registry::ComponentInfos[addData.compId].destructor(addData.buffer.data());
		}
	}
} // namespace Dimensia::ECS