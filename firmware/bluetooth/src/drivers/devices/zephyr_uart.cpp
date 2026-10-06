#include "drivers/devices/zephyr_uart.h"
#include <zephyr/drivers/uart.h>

namespace ble
{
zephyr_uart::zephyr_uart(const device &transport, uart_config config)
	: transport_(transport), config_(config) {}

status zephyr_uart::init()
{
	return fail(component::uart, error_code::not_implemented);
}

void zephyr_uart::stop() {}

status zephyr_uart::write(data_view, std::uint32_t)
{
	return fail(component::uart, error_code::not_implemented);
}

result<std::size_t> zephyr_uart::read(mutable_data_view, std::uint32_t)
{
	return fail(component::uart, error_code::not_implemented);
}
}
