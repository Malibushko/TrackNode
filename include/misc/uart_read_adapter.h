#pragma once
#include "drivers/uart.h"
#include "platform/error.h"

class uart_read_adapter
{
public:
	explicit uart_read_adapter(uart &source) : source_(source)
	{

	}

public:
	uint8_t read()
	{
		uint8_t data;
		size_t read_count = 0;
		if (!source_.read_available(&data, sizeof(data), read_count))
		{
			error_handler(error_code::uart_error);
			return -1;
		}

		return data;
	}

	[[nodiscard]] uint32_t available()
	{
		return source_.available();
	}

private:
	uart& source_;
};