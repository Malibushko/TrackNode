#pragma once

#include "application/command.h"
#include "core/result.h"

class command_sink
{
public:
	virtual ~command_sink() = default;
	virtual status submit(command value) = 0;
};
