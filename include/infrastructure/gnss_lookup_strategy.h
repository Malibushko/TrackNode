#pragma once

#include <array>
#include <cstdint>

class gnss_lookup_strategy
{
public:
	struct attempt
	{
		std::uint32_t search_ms = 0;
		std::uint32_t sleep_ms = 0;
	};

	static constexpr std::array<attempt, 3> backoff{{
	    { 30'000, 120'000 },
	    { 45'000, 300'000 },
	    { 60'000, 900'000 },
	}};
	static constexpr std::uint32_t sleep_after_fix_ms = 300'000;

	void start(std::uint32_t now_ms)
	{
		phase_ = phase::searching;
		entered_at_ms_ = now_ms;
		attempt_index_ = 0;
		fix_found_ = false;
	}

	[[nodiscard]] bool should_lookup(std::uint32_t now_ms) const
	{
		return phase_ == phase::searching && !fix_found_ && elapsed(now_ms) < backoff[attempt_index_].search_ms;
	}

	[[nodiscard]] bool should_sleep(std::uint32_t now_ms) const
	{
		return phase_ == phase::searching && (fix_found_ || elapsed(now_ms) >= backoff[attempt_index_].search_ms);
	}

	[[nodiscard]] std::uint32_t lookup_remaining_ms(std::uint32_t now_ms) const
	{
		if (!should_lookup(now_ms))
			return 0;

		return backoff[attempt_index_].search_ms - elapsed(now_ms);
	}

	void on_fix()
	{
		if (phase_ == phase::searching) fix_found_ = true;
	}

	[[nodiscard]] std::uint32_t sleep_duration_ms() const
	{
		return fix_found_ ? sleep_after_fix_ms : backoff[attempt_index_].sleep_ms;
	}

	// Call only after the UART transfer of the standby command succeeded.
	void on_sleep_started(std::uint32_t now_ms)
	{
		const std::uint32_t duration_ms = sleep_duration_ms();
		if (fix_found_)
			attempt_index_ = 0;
		else if (attempt_index_ + 1 < backoff.size())
			++attempt_index_;

		phase_ = phase::sleeping;
		entered_at_ms_ = now_ms;
		sleep_duration_ = duration_ms;
		fix_found_ = false;
	}

	[[nodiscard]] bool sleeping() const { return phase_ == phase::sleeping; }

	[[nodiscard]] std::uint32_t sleep_remaining_ms(std::uint32_t now_ms) const
	{
		if (!sleeping() || elapsed(now_ms) >= sleep_duration_)
			return 0;

		return sleep_duration_ - elapsed(now_ms);
	}

	void on_wake(std::uint32_t now_ms)
	{
		phase_ = phase::searching;
		entered_at_ms_ = now_ms;
		fix_found_ = false;
	}

	[[nodiscard]] unsigned attempt_index() const { return attempt_index_; }

private:
	enum class phase { searching, sleeping };
	[[nodiscard]] std::uint32_t elapsed(std::uint32_t now_ms) const
	{
		return now_ms - entered_at_ms_;
	}

	phase phase_{phase::searching};
	std::uint32_t entered_at_ms_ = 0;
	std::uint32_t sleep_duration_ = 0;
	unsigned attempt_index_ = 0;
	bool fix_found_ = false;
};
