#pragma once

#include "drivers/uart.h"
#include <array>
#include <optional>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

struct device;

namespace ble
{
struct uart_config
{
	std::uint32_t baud_rate = 0;
	const gpio_dt_spec *wake_input = nullptr;
	std::uint32_t awake_hold_ms = 0;
	std::uint32_t peer_sleep_after_ms = 0;
	std::uint32_t peer_wake_guard_ms = 0;
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
	void set_activity_signal(k_sem *signal);

	status suspend_when_idle();
	void resume_if_woken();
	[[nodiscard]] std::optional<std::uint32_t> suspend_after_ms() const;
	[[nodiscard]] bool suspended() const { return suspended_; }

private:
	static constexpr std::size_t receive_capacity = 1024;

	static void interrupt(const device *transport, void *context);
	void handle_interrupt();
	void signal_activity();

	status transmit(data_view data, std::uint32_t timeout_ms);
	status wake_peer(std::uint32_t timeout_ms);
	status resume();
	void mark_activity();
	[[nodiscard]] std::uint32_t idle_ms() const;
	[[nodiscard]] bool can_suspend() const;
	static void on_wake_edge(const device *port, gpio_callback *callback, std::uint32_t pins);

	struct wake_callback
	{
		gpio_callback callback;
		zephyr_uart *owner;
	};

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

	mutable k_spinlock lock_{};
	k_sem *activity_ = nullptr;

	wake_callback wake_callback_{};
	std::int64_t last_activity_ms_ = 0;
	bool suspended_ = false;
	volatile bool wake_requested_ = false;
	bool wake_callback_added_ = false;
	bool initialized_ = false;
	bool receive_error_ = false;
	bool transmit_error_ = false;
};
}
