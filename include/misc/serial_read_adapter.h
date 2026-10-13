#pragma once

#include "drivers/serial_device.h"

#include <optional>

class serial_read_adapter
{
public:
	explicit serial_read_adapter(serial_device &source) : source_(source) {}

	result<std::optional<std::uint8_t>> read()
	{
		std::uint8_t byte = 0;
		const auto received = source_.read(&byte, 1, 0);
		if (!received)
		{
			return tl::make_unexpected(received.error());
		}
		if (*received == 0)
		{
			return std::optional<std::uint8_t>{};
		}
		if (*received != 1)
		{
			return fail(component::service, error_code::device_error);
		}
		return std::optional<std::uint8_t>{byte};
	}

private:
	serial_device &source_;
};
