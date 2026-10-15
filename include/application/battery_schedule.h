#pragma once

#include <cstdint>

class battery_schedule
{
public:
	struct config
	{
		std::uint32_t update_interval_ms = 300000;
		std::uint32_t retry_interval_ms = 30000;
		std::uint32_t low_load_wait_ms = 60000;
	};

	battery_schedule() = default;
	explicit battery_schedule(config settings) : config_(settings)
	{
	}

	void request(std::uint32_t now_ms)
	{
		if (!pending_)
		{
			pending_ = true;
			requested_at_ms_ = now_ms;
		}
	}

	void poll(std::uint32_t now_ms)
	{
		if (!attempted_ || now_ms - completed_at_ms_ >= interval_ms_)
		{
			request(now_ms);
		}
	}

	[[nodiscard]] bool ready(std::uint32_t now_ms, bool low_load) const
	{
		return pending_ && (low_load || now_ms - requested_at_ms_ >= config_.low_load_wait_ms);
	}

	void complete(std::uint32_t now_ms, bool accepted)
	{
		pending_ = false;
		attempted_ = true;
		completed_at_ms_ = now_ms;
		interval_ms_ = accepted ? config_.update_interval_ms : config_.retry_interval_ms;
	}

	[[nodiscard]] bool pending() const
	{
		return pending_;
	}

private:
	config config_{};
	std::uint32_t requested_at_ms_ = 0;
	std::uint32_t completed_at_ms_ = 0;
	std::uint32_t interval_ms_ = 0;
	bool pending_ = false;
	bool attempted_ = false;
};
