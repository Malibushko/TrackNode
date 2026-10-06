#pragma once

#include "drivers/uart.h"

class Uart;

namespace ble
{
struct uart_config
{
	std::uint32_t baud_rate = 115200;
};

class arduino_uart final : public uart
{
public:
	arduino_uart(Uart &transport, uart_config config);
	status init() override;
	void stop() override;
	status write(data_view data, std::uint32_t timeout_ms) override;
	result<std::size_t> read(mutable_data_view output, std::uint32_t timeout_ms) override;

private:
	Uart &transport_;
	uart_config config_;
};
}
