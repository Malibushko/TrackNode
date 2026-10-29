#pragma once
#include "libs/freertos/freertos_helpers.h"
#include <algorithm>
#include <functional>

class uart
{
public:
	std::deque<std::uint8_t> input;
	std::function<status(const std::uint8_t *, std::size_t)> on_write;
	std::size_t chunk_size = 3;
	std::uint32_t drops = 0;
	bool running = false;

	status init()
	{
		if (running)
		{
			return fail(component::uart, error_code::already_initialized);
		}

		running = true;
		return {};
	}
	std::uint32_t dropped_count() const
	{
		return drops;
	}
	std::size_t available() const
	{
		return input.size();
	}
	void set_rx_listener(TaskHandle_t task)
	{
		rx_listener = task;
	}
	TaskHandle_t rx_listener = nullptr;
	status write(const std::uint8_t *bytes, std::size_t size, std::uint32_t)
	{
		return on_write ? on_write(bytes, size) : status{};
	}
	result<std::size_t> read(std::uint8_t *output, std::size_t capacity, std::uint32_t timeout)
	{
		if (input.empty())
		{
			fake_ticks += timeout;
			return std::size_t{0};
		}

		const auto size = std::min({capacity, chunk_size, input.size()});
		for (std::size_t index = 0; index < size; ++index)
		{
			output[index] = input.front();
			input.pop_front();
		}
		++fake_ticks;
		return size;
	}
};
