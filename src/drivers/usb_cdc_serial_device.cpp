#include "drivers/usb_cdc_serial_device.h"

status usb_cdc_serial_device::init()
{
	return device_.start();
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> usb_cdc_serial_device::read(std::uint8_t *data, std::size_t size, std::uint32_t timeout_ms)
{
	if (timeout_ms != 0)
	{
		return fail(component::usb, error_code::not_implemented);
	}
	if (auto polled = device_.poll(); !polled)
	{
		return tl::make_unexpected(polled.error());
	}
	taskENTER_CRITICAL();
	const auto current_session = device_.session();
	const bool connection_changed = read_session_ && *read_session_ != current_session;
	read_session_ = current_session;
	if (connection_changed)
	{
		taskEXIT_CRITICAL();
		return fail(component::usb, error_code::not_ready);
	}
	const auto received = device_.read(data, size);
	taskEXIT_CRITICAL();
	return received;
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> usb_cdc_serial_device::write(const std::uint8_t *data, std::size_t size, std::uint32_t timeout_ms)
{
	const auto pending = begin_write(data, size);
	if (!pending)
	{
		return tl::make_unexpected(pending.error());
	}
	if (*pending)
	{
		(void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms));
	}
	return finish_write();
}

//////////////////////////////////////////////////////////////////////////

result<bool> usb_cdc_serial_device::begin_write(const std::uint8_t *data, std::size_t size)
{
	taskENTER_CRITICAL();
	if (write_pending_)
	{
		taskEXIT_CRITICAL();
		return fail(component::usb, error_code::not_ready);
	}
	(void)ulTaskNotifyTake(pdTRUE, 0);
	const auto waiter = xTaskGetCurrentTaskHandle();
	const auto accepted = device_.write(data, size, waiter);
	if (!accepted)
	{
		taskEXIT_CRITICAL();
		return tl::make_unexpected(accepted.error());
	}
	write_session_ = device_.session();
	write_size_ = *accepted;
	write_waiter_ = waiter;
	write_pending_ = true;
	taskEXIT_CRITICAL();
	return write_size_ != 0;
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> usb_cdc_serial_device::finish_write()
{
	taskENTER_CRITICAL();
	if (!write_pending_)
	{
		taskEXIT_CRITICAL();
		return fail(component::usb, error_code::not_ready);
	}
	const bool connection_changed = write_session_ != device_.session() || !device_.connected();
	const bool incomplete = device_.tx_busy();
	device_.clear_write_waiter(write_waiter_);
	write_pending_ = false;
	write_waiter_ = nullptr;
	taskEXIT_CRITICAL();
	if (connection_changed)
	{
		return fail(component::usb, error_code::not_ready);
	}
	if (incomplete)
	{
		return fail(component::usb, error_code::timeout_error, HAL_TIMEOUT);
	}
	return write_size_;
}

//////////////////////////////////////////////////////////////////////////

void usb_cdc_serial_device::cancel_write()
{
	taskENTER_CRITICAL();
	if (write_pending_)
	{
		device_.clear_write_waiter(write_waiter_);
		write_pending_ = false;
		write_waiter_ = nullptr;
	}
	taskEXIT_CRITICAL();
}

//////////////////////////////////////////////////////////////////////////
