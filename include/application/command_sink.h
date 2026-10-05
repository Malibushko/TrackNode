#pragma once

#include "application/command_reply.h"
#include "core/result.h"

class command_sink
{
public:
	virtual ~command_sink() = default;
	virtual status submit(command_request request) = 0;
	status submit(command value) { return submit(command_request{value}); }
};
