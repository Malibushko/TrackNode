#include "drivers/serial_device.h"

serial_io_operation::serial_io_operation(serial_device &owner, std::uint8_t *input,
	const std::uint8_t *output, std::size_t size, direction operation)
	: owner_(owner), input_(input), output_(output), size_(size), direction_(operation) {}

//////////////////////////////////////////////////////////////////////////

serial_io_operation::~serial_io_operation()
{
	if (active_)
	{
		if (direction_ == direction::read)
		{
			owner_.cancel_read();
		}
		else
		{
			owner_.cancel_write();
		}
	}
}

//////////////////////////////////////////////////////////////////////////

bool serial_io_operation::await_ready()
{
	started_ = direction_ == direction::read ? owner_.begin_read(input_, size_) : owner_.begin_write(output_, size_);
	active_ = started_.has_value();
	pending_ = started_ && *started_;
	return !pending_;
}

//////////////////////////////////////////////////////////////////////////

void serial_io_operation::await_suspend(std::coroutine_handle<>) const
{
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> serial_io_operation::await_resume()
{
	pending_ = false;
	active_ = false;
	if (!started_)
	{
		return tl::make_unexpected(started_.error());
	}
	return direction_ == direction::read ? owner_.finish_read() : owner_.finish_write();
}

//////////////////////////////////////////////////////////////////////////

serial_io_operation serial_device::read_async(std::uint8_t *data, std::size_t size)
{
	return serial_io_operation{*this, data, nullptr, size, serial_io_operation::direction::read};
}

//////////////////////////////////////////////////////////////////////////

serial_io_operation serial_device::write_async(const std::uint8_t *data, std::size_t size)
{
	return serial_io_operation{*this, nullptr, data, size, serial_io_operation::direction::write};
}

//////////////////////////////////////////////////////////////////////////

result<bool> serial_device::begin_read(std::uint8_t *, std::size_t)
{
	return fail(source_, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> serial_device::finish_read()
{
	return fail(source_, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

void serial_device::cancel_read()
{
}

//////////////////////////////////////////////////////////////////////////

result<bool> serial_device::begin_write(const std::uint8_t *, std::size_t)
{
	return fail(source_, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

result<std::size_t> serial_device::finish_write()
{
	return fail(source_, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

void serial_device::cancel_write()
{
}

//////////////////////////////////////////////////////////////////////////
