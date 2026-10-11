#pragma once

#include "drivers/uart.h"
#include <array>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

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
	zephyr_uart(const zephyr_uart &) = delete;
	zephyr_uart &operator=(const zephyr_uart &) = delete;

	status init() override;
	void stop() override;

	status write(data_view data, std::uint32_t timeout_ms) override;
	result<std::size_t> read(mutable_data_view output, std::uint32_t timeout_ms) override;

private:
	static constexpr std::size_t receive_capacity = 1024;

	static void interrupt(const device *transport, void *context);
	void handle_interrupt();

private:
	const device &transport_;
	uart_config config_;

	std::array<std::uint8_t, receive_capacity> receive_storage_{};
	ring_buf receive_buffer_{};
	k_sem receive_ready_{};
	k_mutex receive_mutex_{};

	k_sem transmit_done_{};
	k_mutex transmit_mutex_{};
	const std::uint8_t *transmit_data_ = nullptr;
	std::size_t transmit_remaining_ = 0;

	k_spinlock lock_{};
	bool initialized_ = false;
	bool receive_error_ = false;
	bool transmit_error_ = false;
};
}
