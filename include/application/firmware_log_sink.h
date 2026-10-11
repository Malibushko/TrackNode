#pragma once

#include <string_view>

class firmware_log_sink
{
public:
	virtual ~firmware_log_sink() = default;
	virtual void submit_log(std::string_view text) = 0;
};
