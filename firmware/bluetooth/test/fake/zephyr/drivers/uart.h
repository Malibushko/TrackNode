#pragma once
#include <cstdint>
#include <deque>
#include <vector>
#include <algorithm>
struct device;
using uart_irq_callback_user_data_t = void (*)(const device *, void *);
struct device
{
	bool ready = true;
	int configure_result = 0;
	int callback_result = 0;
	bool rx = false;
	bool tx = false;
	bool errors_enabled = false;
	bool stalled = false;
	bool fill_failure = false;
	int errors = 0;
	uart_irq_callback_user_data_t callback = nullptr;
	void *context = nullptr;
	std::deque<std::uint8_t> incoming;
	std::vector<std::uint8_t> outgoing;
};
struct uart_config
{
	std::uint32_t baudrate;
	int parity;
	int stop_bits;
	int data_bits;
	int flow_ctrl;
};
inline constexpr int UART_CFG_PARITY_NONE = 0;
inline constexpr int UART_CFG_STOP_BITS_1 = 1;
inline constexpr int UART_CFG_DATA_BITS_8 = 8;
inline constexpr int UART_CFG_FLOW_CTRL_NONE = 0;
inline device &state(const device *d)
{
	return *const_cast<device *>(d);
}
inline bool device_is_ready(const device *d)
{
	return d->ready;
}
inline int uart_configure(const device *d, const uart_config *)
{
	return d->configure_result;
}
inline int uart_irq_callback_user_data_set(const device *d, uart_irq_callback_user_data_t callback, void *context)
{
	if (d->callback_result != 0)
	{
		return d->callback_result;
	}
	state(d).callback = callback;
	state(d).context = context;
	return 0;
}
inline void uart_irq_rx_enable(const device *d) { state(d).rx = true; }
inline void uart_irq_rx_disable(const device *d) { state(d).rx = false; }
inline void uart_irq_tx_enable(const device *d) { state(d).tx = true; }
inline void uart_irq_tx_disable(const device *d) { state(d).tx = false; }
inline void uart_irq_err_enable(const device *d) { state(d).errors_enabled = true; }
inline void uart_irq_err_disable(const device *d) { state(d).errors_enabled = false; }
inline int uart_irq_update(const device *)
{
	return 1;
}
inline int uart_irq_rx_ready(const device *d)
{
	return d->rx && !d->incoming.empty();
}
inline int uart_irq_tx_ready(const device *d)
{
	return d->tx && !d->stalled;
}
inline int uart_irq_tx_complete(const device *d)
{
	return uart_irq_tx_ready(d);
}
inline int uart_err_check(const device *d)
{
	const int errors = d->errors;
	state(d).errors = 0;
	return errors;
}
inline int uart_fifo_read(const device *d, std::uint8_t *bytes, int size)
{
	int count = 0;
	while (count < size && !d->incoming.empty())
	{
		bytes[count++] = d->incoming.front();
		state(d).incoming.pop_front();
	}
	return count;
}
inline int uart_fifo_fill(const device *d, const std::uint8_t *bytes, int size)
{
	if (d->fill_failure)
	{
		return -EIO;
	}
	const int count = std::min(size, 7);
	state(d).outgoing.insert(state(d).outgoing.end(), bytes, bytes + count);
	return count;
}
inline void fire(device &d)
{
	if (d.callback != nullptr && (d.rx || d.tx || d.errors_enabled))
	{
		d.callback(&d, d.context);
	}
}
