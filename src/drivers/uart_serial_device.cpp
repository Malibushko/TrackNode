#include "drivers/uart_serial_device.h"

result<std::size_t> uart_serial_device::read(std::uint8_t *data, std::size_t size, std::uint32_t timeout_ms)
{
	return device_.read(data, size, timeout_ms);
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> uart_serial_device::write(const std::uint8_t *data, std::size_t size, std::uint32_t timeout_ms)
{
	if (write_operation_)
	{
		return fail(component::uart, error_code::not_ready);
	}
	if (auto sent = device_.write(data, size, timeout_ms); !sent)
	{
		return tl::make_unexpected(sent.error());
	}
	return size;
}

//////////////////////////////////////////////////////////////////////////

result<bool> uart_serial_device::begin_write(const std::uint8_t *data, std::size_t size)
{
	if (write_operation_)
	{
		return fail(component::uart, error_code::not_ready);
	}
	write_size_ = size;
	write_operation_.emplace(device_.write_async(data, size));
	if (write_operation_->await_ready())
	{
		return false;
	}
	if (!write_operation_->await_suspend({}))
	{
		const auto failed = write_operation_->await_resume();
		write_operation_.reset();
		if (!failed)
		{
			return tl::make_unexpected(failed.error());
		}
		return fail(component::uart, error_code::device_error);
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> uart_serial_device::finish_write()
{
	if (!write_operation_)
	{
		return fail(component::uart, error_code::not_ready);
	}
	const auto completed = write_operation_->await_resume();
	write_operation_.reset();
	if (!completed)
	{
		return tl::make_unexpected(completed.error());
	}
	return write_size_;
}

//////////////////////////////////////////////////////////////////////////

void uart_serial_device::cancel_write()
{
	(void)finish_write();
}

//////////////////////////////////////////////////////////////////////////
