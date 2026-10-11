#pragma once
#include "core/result.h"
#include "application/firmware_log_sink.h"
class logger_service
{
public:
	firmware_log_sink *sink = nullptr;
	void set_log_sink(firmware_log_sink *value)
	{
		sink = value;
	}
	status write(const char *, ...)
	{
		return {};
	}
};
