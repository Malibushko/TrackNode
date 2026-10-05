#pragma once

#include "infrastructure/supervisor.h"
#include "application/command_reply.h"
#include "drivers/uart.h"

#include <atomic>
#include <array>
#include <cstddef>
#include <coroutine>

class logger_service final : public command_reply_sink
{
public:
	logger_service(uart &output, supervisor &monitor) : output_(output), monitor_(monitor) {}

public: // Common lifecycle
	status start();
	void stop();

public: // Logger API
	status write(const char *format, ...);
	status submit_reply(const command_reply &reply) override;

	[[nodiscard]] std::uint32_t dropped_count() const { return dropped_.load(); }
	[[nodiscard]] std::uint32_t failed_writes() const { return failed_writes_.load(); }

private: // Logger internals
	struct tx_operation
	{
		struct promise_type
		{
			logger_service *owner;
			status outcome{};
			promise_type(logger_service *owner, const std::uint8_t *, std::size_t) : owner(owner) {}
			~promise_type() { owner->tx_frame_busy_ = false; }
			tx_operation get_return_object();
			static tx_operation get_return_object_on_allocation_failure() noexcept;
			std::suspend_never initial_suspend() noexcept { return {}; }
			std::suspend_always final_suspend() noexcept { return {}; }
			void return_value(status value) { outcome = value; }
			void unhandled_exception() { __builtin_trap(); }
			static void *operator new(std::size_t size, logger_service *owner,
			                          const std::uint8_t *, std::size_t) noexcept;
			static void operator delete(void *, std::size_t) noexcept {}
		};

		std::coroutine_handle<promise_type> handle{};
		status allocation_error = fail(component::rtos, error_code::out_of_memory);
		explicit tx_operation(std::coroutine_handle<promise_type> value) : handle(value) {}
		tx_operation() = default;
		tx_operation(const tx_operation &) = delete;
		tx_operation(tx_operation &&other) noexcept : handle(other.handle), allocation_error(other.allocation_error)
		{ other.handle = {}; }
		~tx_operation() { if (handle) handle.destroy(); }
		[[nodiscard]] bool done() const { return !handle || handle.done(); }
		void resume() { handle.resume(); }
		[[nodiscard]] status result() const { return handle ? handle.promise().outcome : allocation_error; }
	};

	struct message { char text[96]; };
	static tx_operation send_async(logger_service *owner, const std::uint8_t *data, std::size_t size);

private: // Common task loop
	static void task_entry(void *argument);
	void run();

private: // State
	uart &output_;
	supervisor &monitor_;
	QueueHandle_t inbox_ = nullptr;
	TaskHandle_t task_ = nullptr;
	std::atomic<std::uint32_t> dropped_{ 0 };
	std::atomic<std::uint32_t> failed_writes_{ 0 };
	alignas(std::max_align_t) std::array<std::byte, 256> tx_frame_{};
	bool tx_frame_busy_ = false;
};
