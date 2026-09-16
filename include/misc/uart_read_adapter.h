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
		auto read_result = source_.read(&data, sizeof(data));

		if (!read_result)
		{
			error_ = read_result.error();
		}
		else if (read_result.value() != 1)
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
