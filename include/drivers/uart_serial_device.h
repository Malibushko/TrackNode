#pragma once

#include "drivers/serial_device.h"
#include "drivers/uart.h"

#include <optional>

class uart_serial_device final : public serial_device
{
public:
	explicit uart_serial_device(uart &device) : serial_device(component::uart), device_(device) {}
	result<std::size_t> read(std::uint8_t *data, std::size_t size, std::uint32_t timeout_ms = 0) override;
	result<std::size_t> write(const std::uint8_t *data, std::size_t size, std::uint32_t timeout_ms = 100) override;

private:
	result<bool> begin_write(const std::uint8_t *data, std::size_t size) override;
	result<std::size_t> finish_write() override;
	void cancel_write() override;

private:
	uart &device_;
	std::optional<uart::tx_awaitable> write_operation_;
	std::size_t write_size_ = 0;
};
