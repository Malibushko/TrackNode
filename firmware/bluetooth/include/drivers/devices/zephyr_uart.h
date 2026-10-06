#pragma once

#include "drivers/uart.h"

struct device;

namespace ble
{
struct uart_config
{
	std::uint32_t baud_rate = 0;
};

class zephyr_uart final : public uart
{
public:
	zephyr_uart(const device &transport, uart_config config);
	status init() override;
	void stop() override;
	status write(data_view data, std::uint32_t timeout_ms) override;
	result<std::size_t> read(mutable_data_view output, std::uint32_t timeout_ms) override;

private:
	const device &transport_;
	uart_config config_;
};
}
