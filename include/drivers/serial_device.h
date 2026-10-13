#pragma once

#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <coroutine>

class serial_device;

class serial_io_operation
{
public:
	serial_io_operation(const serial_io_operation &) = delete;
	serial_io_operation &operator=(const serial_io_operation &) = delete;
	serial_io_operation(serial_io_operation &&) = delete;
	serial_io_operation &operator=(serial_io_operation &&) = delete;
	~serial_io_operation();

	bool await_ready();
	void await_suspend(std::coroutine_handle<>) const;
	result<std::size_t> await_resume();

private:
	enum class direction { read, write };
	serial_io_operation(serial_device &owner, std::uint8_t *input,
		const std::uint8_t *output, std::size_t size, direction operation);

private:
	serial_device &owner_;
	std::uint8_t *input_;
	const std::uint8_t *output_;
	std::size_t size_;
	direction direction_;
	result<bool> started_{false};
	bool pending_ = false;
	bool active_ = false;
	friend class serial_device;
};

class serial_device
{
public:
	virtual ~serial_device() = default;
	serial_device(const serial_device &) = delete;
	serial_device &operator=(const serial_device &) = delete;

	virtual result<std::size_t> read(std::uint8_t *data, std::size_t size, std::uint32_t timeout_ms = 0) = 0;
	virtual result<std::size_t> write(const std::uint8_t *data, std::size_t size, std::uint32_t timeout_ms = 100) = 0;
	[[nodiscard]] serial_io_operation read_async(std::uint8_t *data, std::size_t size);
	[[nodiscard]] serial_io_operation write_async(const std::uint8_t *data, std::size_t size);

protected:
	explicit serial_device(component source) : source_(source) {}
	virtual result<bool> begin_read(std::uint8_t *data, std::size_t size);
	virtual result<std::size_t> finish_read();
	virtual void cancel_read();
	virtual result<bool> begin_write(const std::uint8_t *data, std::size_t size);
	virtual result<std::size_t> finish_write();
	virtual void cancel_write();
	[[nodiscard]] component source() const { return source_; }

private:
	component source_;
	friend class serial_io_operation;
};
