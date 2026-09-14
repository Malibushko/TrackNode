#pragma once

#include <coroutine>
#include <cstdint>

#include "common/common.h"

class status_blink_task
{
public:
	struct promise_type
	{
		uint32_t wake_at_ms = 0;

		status_blink_task get_return_object()
		{
			return status_blink_task{std::coroutine_handle<promise_type>::from_promise(*this)};
		}

		std::suspend_always initial_suspend() noexcept { return {}; }
		std::suspend_always final_suspend() noexcept { return {}; }
		void return_void() noexcept {}
		void unhandled_exception() noexcept {}
	};

	struct delay_ms
	{
		uint32_t duration;

		bool await_ready() const noexcept { return duration == 0; }

		void await_suspend(std::coroutine_handle<promise_type> handle) const noexcept
		{
			handle.promise().wake_at_ms = common::get_tick() + duration;
		}

		void await_resume() const noexcept {}
	};

	explicit status_blink_task(std::coroutine_handle<promise_type> handle) : handle_{handle} {}

	status_blink_task(const status_blink_task &) = delete;
	status_blink_task &operator=(const status_blink_task &) = delete;

	status_blink_task(status_blink_task &&other) noexcept : handle_{other.handle_}
	{
		other.handle_ = nullptr;
	}

	~status_blink_task()
	{
		if (handle_)
		{
			handle_.destroy();
		}
	}

	void poll()
	{
		if (!handle_ || handle_.done())
		{
			return;
		}
		if (common::get_tick() >= handle_.promise().wake_at_ms)
		{
			handle_.resume();
		}
	}

	[[nodiscard]] bool done() const noexcept
	{
		return !handle_ || handle_.done();
	}

private:
	std::coroutine_handle<promise_type> handle_;
};

