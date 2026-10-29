#include "drivers/devices/zephyr_uart.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <zephyr/drivers/uart.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/clock.h>

namespace ble
{
namespace
{
error_code configuration_error(int code)
{
	if (code == -ENOSYS || code == -ENOTSUP)
	{
		return error_code::not_supported;
	}

	if (code == -EINVAL)
	{
		return error_code::invalid_config;
	}

	return error_code::io;
}
}

zephyr_uart::zephyr_uart(const device &transport, uart_config config)
	: transport_(transport), config_(config)
{
	ring_buf_init(&receive_buffer_, receive_storage_.size(), receive_storage_.data());
	k_sem_init(&receive_ready_, 0, 1);
	k_sem_init(&transmit_done_, 0, 1);
	k_mutex_init(&receive_mutex_);
	k_mutex_init(&transmit_mutex_);
}

//////////////////////////////////////////////////////////////////////////

status zephyr_uart::init()
{
	if (!device_is_ready(&transport_))
	{
		return fail(component::uart, error_code::not_ready);
	}

	if (config_.baud_rate == 0)
	{
		return fail(component::uart, error_code::invalid_config);
	}

	if (initialized_)
	{
		return {};
	}

	::uart_config config{
		.baudrate = config_.baud_rate,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE
	};

	const int configured = uart_configure(&transport_, &config);
	if (configured != 0)
	{
		return fail(component::uart, configuration_error(configured));
	}

	const int registered = uart_irq_callback_user_data_set(&transport_, interrupt, this);
	if (registered != 0)
	{
		return fail(component::uart, configuration_error(registered));
	}

	if (config_.wake_input && !wake_callback_added_)
	{
		wake_callback_.owner = this;
		gpio_init_callback(&wake_callback_.callback, on_wake_edge, BIT(config_.wake_input->pin));
		if (const int added = gpio_add_callback_dt(config_.wake_input, &wake_callback_.callback); added != 0)
		{
			return fail(component::uart, configuration_error(added));
		}
		wake_callback_added_ = true;
	}

	ring_buf_reset(&receive_buffer_);
	k_sem_reset(&receive_ready_);
	k_sem_reset(&transmit_done_);
	receive_error_ = false;
	transmit_error_ = false;
	initialized_ = true;

	uart_irq_rx_enable(&transport_);
	uart_irq_err_enable(&transport_);

	return {};
}

//////////////////////////////////////////////////////////////////////////

void zephyr_uart::stop()
{
	if (suspended_)
	{
		(void)resume();
	}

	const auto key = k_spin_lock(&lock_);
	if (initialized_)
	{
		uart_irq_rx_disable(&transport_);
		uart_irq_err_disable(&transport_);
		uart_irq_tx_disable(&transport_);
		uart_irq_callback_user_data_set(&transport_, nullptr, nullptr);
		initialized_ = false;
		transmit_data_ = nullptr;
		transmit_remaining_ = 0;
		ring_buf_reset(&receive_buffer_);
	}
	k_spin_unlock(&lock_, key);

	k_sem_give(&receive_ready_);
	k_sem_give(&transmit_done_);
}

//////////////////////////////////////////////////////////////////////////


status zephyr_uart::write(data_view data, std::uint32_t timeout_ms)
{
	const auto peer_may_sleep = config_.peer_sleep_after_ms != 0 && idle_ms() >= config_.peer_sleep_after_ms;
	if (suspended_)
	{
		if (auto resumed = resume(); !resumed)
		{
			return resumed;
		}
	}
	if (peer_may_sleep)
	{
		if (auto woken = wake_peer(timeout_ms); !woken)
		{
			return woken;
		}
	}

	const auto sent = transmit(data, timeout_ms);
	mark_activity();
	return sent;
}

//////////////////////////////////////////////////////////////////////////

status zephyr_uart::transmit(data_view data, std::uint32_t timeout_ms)
{
	if (data.size != 0 && data.data == nullptr)
	{
		return fail(component::uart, error_code::invalid_config);
	}

	const auto deadline = sys_timepoint_calc(K_MSEC(timeout_ms));
	if (k_mutex_lock(&transmit_mutex_, sys_timepoint_timeout(deadline)) != 0)
	{
		return fail(component::uart, error_code::timeout);
	}

	const auto key = k_spin_lock(&lock_);
	if (!initialized_ || data.size == 0)
	{
		const bool ready = initialized_;
		k_spin_unlock(&lock_, key);
		k_mutex_unlock(&transmit_mutex_);
		if (!ready)
		{
			return fail(component::uart, error_code::not_initialized);
		}

		return {};
	}

	const auto transmit_deadline_expired = timeout_ms == 0 || sys_timepoint_expired(deadline);
	if (transmit_deadline_expired)
	{
		k_spin_unlock(&lock_, key);
		k_mutex_unlock(&transmit_mutex_);
		return fail(component::uart, error_code::timeout);
	}

	k_sem_reset(&transmit_done_);
	transmit_data_ = data.data;
	transmit_remaining_ = data.size;
	transmit_error_ = false;
	uart_irq_tx_enable(&transport_);
	k_spin_unlock(&lock_, key);

	const int completed = k_sem_take(&transmit_done_, sys_timepoint_timeout(deadline));
	const auto completion_key = k_spin_lock(&lock_);
	const bool ready = initialized_;
	const bool failed = transmit_error_;
	uart_irq_tx_disable(&transport_);
	transmit_data_ = nullptr;
	transmit_remaining_ = 0;
	k_spin_unlock(&lock_, completion_key);
	k_mutex_unlock(&transmit_mutex_);

	if (!ready)
	{
		return fail(component::uart, error_code::not_initialized);
	}

	if (completed != 0)
	{
		return fail(component::uart, error_code::timeout);
	}

	if (failed)
	{
		return fail(component::uart, error_code::io);
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> zephyr_uart::read(mutable_data_view output, std::uint32_t timeout_ms)
{
	if (output.capacity != 0 && output.data == nullptr)
	{
		return fail(component::uart, error_code::invalid_config);
	}

	const auto deadline = sys_timepoint_calc(K_MSEC(timeout_ms));
	if (k_mutex_lock(&receive_mutex_, sys_timepoint_timeout(deadline)) != 0)
	{
		return fail(component::uart, error_code::timeout);
	}

	while (true)
	{
		const auto key = k_spin_lock(&lock_);
		const auto receive_stream_unavailable = !initialized_ || receive_error_;
		if (receive_stream_unavailable)
		{
			const auto code = initialized_ ? error_code::io : error_code::not_initialized;
			receive_error_ = false;
			ring_buf_reset(&receive_buffer_);
			k_spin_unlock(&lock_, key);
			k_mutex_unlock(&receive_mutex_);
			return fail(component::uart, code);
		}

		const auto capacity = std::min(output.capacity, receive_storage_.size());
		const auto received = ring_buf_get(&receive_buffer_, output.data, capacity);
		k_spin_unlock(&lock_, key);

		if (received != 0 || output.capacity == 0)
		{
			k_mutex_unlock(&receive_mutex_);
			return received;
		}

		if (k_sem_take(&receive_ready_, sys_timepoint_timeout(deadline)) != 0)
		{
			k_mutex_unlock(&receive_mutex_);
			return fail(component::uart, error_code::timeout);
		}
	}
}

//////////////////////////////////////////////////////////////////////////


void zephyr_uart::set_activity_signal(k_sem *signal)
{
	const auto key = k_spin_lock(&lock_);
	activity_ = signal;
	k_spin_unlock(&lock_, key);
}

//////////////////////////////////////////////////////////////////////////

status zephyr_uart::suspend_when_idle()
{
	const auto hold_running = idle_ms() < config_.awake_hold_ms;
	if (!can_suspend() || suspended_ || hold_running)
	{
		return {};
	}

	if (const int err = pm_device_action_run(&transport_, PM_DEVICE_ACTION_SUSPEND); err != 0 && err != -EALREADY)
	{
		return fail(component::uart, configuration_error(err));
	}
	suspended_ = true;
	wake_requested_ = false;
	const auto wake_input_armed = gpio_pin_configure_dt(config_.wake_input, GPIO_INPUT) == 0 &&
		gpio_pin_interrupt_configure_dt(config_.wake_input, GPIO_INT_LEVEL_ACTIVE) == 0;
	if (!wake_input_armed)
	{
		return resume();
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

void zephyr_uart::resume_if_woken()
{
	if (suspended_ && wake_requested_)
	{
		(void)resume();
	}
}

//////////////////////////////////////////////////////////////////////////

std::optional<std::uint32_t> zephyr_uart::suspend_after_ms() const
{
	if (!can_suspend() || suspended_)
	{
		return std::nullopt;
	}

	const auto idle = idle_ms();
	return idle >= config_.awake_hold_ms ? 0u : config_.awake_hold_ms - idle;
}

//////////////////////////////////////////////////////////////////////////

status zephyr_uart::wake_peer(std::uint32_t timeout_ms)
{
	const std::uint8_t delimiter = 0;
	if (auto sent = transmit({&delimiter, 1}, timeout_ms); !sent)
	{
		return sent;
	}
	k_msleep(static_cast<std::int32_t>(config_.peer_wake_guard_ms));
	return {};
}

//////////////////////////////////////////////////////////////////////////

status zephyr_uart::resume()
{
	(void)gpio_pin_interrupt_configure_dt(config_.wake_input, GPIO_INT_DISABLE);
	const int err = pm_device_action_run(&transport_, PM_DEVICE_ACTION_RESUME);
	suspended_ = false;
	wake_requested_ = false;
	mark_activity();
	if (err != 0 && err != -EALREADY)
	{
		return fail(component::uart, configuration_error(err));
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

void zephyr_uart::mark_activity()
{
	const auto key = k_spin_lock(&lock_);
	last_activity_ms_ = k_uptime_get();
	k_spin_unlock(&lock_, key);
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t zephyr_uart::idle_ms() const
{
	const auto key = k_spin_lock(&lock_);
	const auto idle = k_uptime_get() - last_activity_ms_;
	k_spin_unlock(&lock_, key);
	return static_cast<std::uint32_t>(std::max<std::int64_t>(idle, 0));
}

//////////////////////////////////////////////////////////////////////////

bool zephyr_uart::can_suspend() const
{
	return initialized_ && config_.wake_input != nullptr && config_.awake_hold_ms != 0;
}

//////////////////////////////////////////////////////////////////////////

void zephyr_uart::on_wake_edge(const device *, gpio_callback *callback, std::uint32_t)
{
	auto &self = *reinterpret_cast<wake_callback *>(callback)->owner;
	(void)gpio_pin_interrupt_configure_dt(self.config_.wake_input, GPIO_INT_DISABLE);
	self.wake_requested_ = true;
	self.signal_activity();
}

//////////////////////////////////////////////////////////////////////////

void zephyr_uart::signal_activity()
{
	if (activity_)
	{
		k_sem_give(activity_);
	}
}

//////////////////////////////////////////////////////////////////////////

void zephyr_uart::interrupt(const device *, void *context)
{
	static_cast<zephyr_uart *>(context)->handle_interrupt();
}

//////////////////////////////////////////////////////////////////////////

void zephyr_uart::handle_interrupt()
{
	const auto key = k_spin_lock(&lock_);
	const auto interrupt_cannot_be_processed = !initialized_ || uart_irq_update(&transport_) <= 0;
	if (interrupt_cannot_be_processed)
	{
		k_spin_unlock(&lock_, key);
		return;
	}

	if (uart_err_check(&transport_) != 0)
	{
		receive_error_ = true;
		k_sem_give(&receive_ready_);
		signal_activity();
	}

	if (uart_irq_rx_ready(&transport_) > 0)
	{
		std::array<std::uint8_t, 32> incoming{};
		int received = 0;
		do
		{
			received = uart_fifo_read(&transport_, incoming.data(), incoming.size());
			if (received > 0)
			{
				if (ring_buf_put(&receive_buffer_, incoming.data(), received) != static_cast<std::uint32_t>(received))
				{
					receive_error_ = true;
				}
				k_sem_give(&receive_ready_);
				last_activity_ms_ = k_uptime_get();
				signal_activity();
			}
		} while (received > 0);

		if (received < 0)
		{
			receive_error_ = true;
			k_sem_give(&receive_ready_);
			signal_activity();
		}
	}

	const auto transmit_interrupt_ready = transmit_data_ != nullptr && uart_irq_tx_ready(&transport_) > 0;
	if (transmit_interrupt_ready)
	{
		if (transmit_remaining_ == 0)
		{
			if (uart_irq_tx_complete(&transport_) > 0)
			{
				uart_irq_tx_disable(&transport_);
				transmit_data_ = nullptr;
				k_sem_give(&transmit_done_);
			}
		}
		else
		{
			const auto count = std::min(transmit_remaining_, static_cast<std::size_t>(INT_MAX));
			const int sent = uart_fifo_fill(&transport_, transmit_data_, count);
			if (sent <= 0)
			{
				transmit_error_ = true;
				uart_irq_tx_disable(&transport_);
				transmit_data_ = nullptr;
				k_sem_give(&transmit_done_);
			}
			else
			{
				transmit_data_ += sent;
				transmit_remaining_ -= sent;
			}
		}
	}

	k_spin_unlock(&lock_, key);
}
}
