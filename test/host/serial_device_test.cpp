#include "drivers/serial_device.h"
#include "misc/serial_read_adapter.h"

#include <cassert>

class fake_serial final : public serial_device
{
public:
	fake_serial() : serial_device(component::uart) {}
	result<std::size_t> read(std::uint8_t *data, std::size_t, std::uint32_t) override
	{
		if (read_failure)
		{
			return fail(component::uart, error_code::device_error);
		}
		if (read_count)
		{
			*data = byte;
		}
		return read_count;
	}
	result<std::size_t> write(const std::uint8_t *, std::size_t size, std::uint32_t) override
	{
		return size;
	}

	std::uint8_t byte = 0;
	std::size_t read_count = 0;
	bool read_failure = false;
	bool immediate_write = false;
	bool pending_write = false;
	int cancelled_writes = 0;

private:
	result<bool> begin_write(const std::uint8_t *, std::size_t size) override
	{
		if (pending_write)
		{
			return fail(component::uart, error_code::not_ready);
		}
		pending_write = true;
		write_size_ = size;
		return !immediate_write;
	}
	result<std::size_t> finish_write() override
	{
		pending_write = false;
		return write_size_;
	}
	void cancel_write() override
	{
		pending_write = false;
		++cancelled_writes;
	}

private:
	std::size_t write_size_ = 0;
};

struct serial_probe
{
	struct promise_type
	{
		result<std::size_t> outcome{0};
		serial_probe get_return_object()
		{
			return {std::coroutine_handle<promise_type>::from_promise(*this)};
		}
		std::suspend_never initial_suspend() { return {}; }
		std::suspend_always final_suspend() noexcept { return {}; }
		void return_value(result<std::size_t> value) { outcome = value; }
		void unhandled_exception() { __builtin_trap(); }
	};
	std::coroutine_handle<promise_type> handle;
	~serial_probe() { handle.destroy(); }
};

//////////////////////////////////////////////////////////////////////////

serial_probe async_read(serial_device &transport, std::uint8_t &byte)
{
	co_return co_await transport.read_async(&byte, 1);
}

//////////////////////////////////////////////////////////////////////////

serial_probe async_write(serial_device &transport, const std::uint8_t &byte)
{
	co_return co_await transport.write_async(&byte, 1);
}

//////////////////////////////////////////////////////////////////////////

int main()
{
	fake_serial device;
	serial_device &transport = device;
	serial_read_adapter reader{transport};
	assert(reader.read() && !*reader.read());
	device.read_count = 1;
	const auto zero_byte = reader.read();
	assert(zero_byte && *zero_byte && **zero_byte == 0);
	device.read_failure = true;
	assert(!reader.read());
	device.read_failure = false;
	device.read_count = 2;
	assert(!reader.read());
	std::uint8_t byte = 42;
	{
		auto unsupported = async_read(transport, byte);
		assert(unsupported.handle.done());
		const auto &outcome = unsupported.handle.promise().outcome;
		assert(!outcome && outcome.error().component == component::uart &&
			outcome.error().code == error_code::not_implemented);
	}
	{
		auto pending = async_write(transport, byte);
		assert(!pending.handle.done() && device.pending_write);
		pending.handle.resume();
		assert(pending.handle.done() && pending.handle.promise().outcome == 1);
		assert(!device.pending_write);
	}
	{
		auto abandoned = async_write(transport, byte);
		assert(!abandoned.handle.done());
	}
	assert(device.cancelled_writes == 1 && !device.pending_write);
	device.immediate_write = true;
	{
		auto completed = async_write(transport, byte);
		assert(completed.handle.done() && completed.handle.promise().outcome == 1);
	}
	{
		auto ready_but_abandoned = transport.write_async(&byte, 1);
		assert(ready_but_abandoned.await_ready());
	}
	assert(device.cancelled_writes == 2 && !device.pending_write);
}
