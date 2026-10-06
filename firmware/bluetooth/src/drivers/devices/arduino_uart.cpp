#include "drivers/devices/arduino_uart.h"
#include <Arduino.h>

namespace ble
{
arduino_uart::arduino_uart(Uart &transport, uart_config config)
	: transport_(transport), config_(config) {}

status arduino_uart::init()
{
	return fail(component::uart, error_code::not_implemented);
}

void arduino_uart::stop() {}

status arduino_uart::write(data_view, std::uint32_t)
{
	return fail(component::uart, error_code::not_implemented);
}

result<std::size_t> arduino_uart::read(mutable_data_view, std::uint32_t)
{
	return fail(component::uart, error_code::not_implemented);
}
}
