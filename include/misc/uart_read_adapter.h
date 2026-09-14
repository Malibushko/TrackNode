#pragma once

#include "drivers/uart.h"
#include <optional>

class uart_read_adapter
{
public:
	explicit uart_read_adapter(uart &source) : source_(source)
	{
	}

	uint8_t read()
	{
		uint8_t data = 0;
		size_t read_count = 0;
		auto read_status = source_.read_available(&data, sizeof(data), read_count);

		if (!read_status)
		{
			error_ = read_status.error();
		}
		else if (read_count != 1)
		{
			error_ = error{ component::uart, error_code::uart_error };
		}

		return data;
	}

	[[nodiscard]] size_t available() const
	{
		return error_ ? 0 : source_.available();
	}

	[[nodiscard]] const std::optional<error> &last_error() const
	{
		return error_;
	}

private:
	uart &source_;
	std::optional<error> error_;
};
