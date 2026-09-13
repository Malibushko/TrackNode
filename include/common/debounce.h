#pragma once

#include <concepts>
#include <cstdint>

template <typename T>
concept debounce_source = requires(const T& t) {
	{ !t.read() } -> std::convertible_to<bool>;
	{ t.read().value() } -> std::convertible_to<bool>;
};

template <debounce_source source, uint32_t ms>
class debounced_input
{
public:
	explicit debounced_input(source& src) : src_(src) {}

	void update(uint32_t now_ms)
	{
		auto raw = src_.read();
		if (!raw)
		{
			return;
		}

		const bool raw_state = raw.value();

		if (raw_state != last_raw_)
		{
			last_raw_ = raw_state;
			last_change_ms_ = now_ms;
		}
		else if (static_cast<uint32_t>(now_ms - last_change_ms_) >= ms && raw_state != stable_state_)
		{
			stable_state_ = raw_state;
			changed_ = true;
		}
	}

	[[nodiscard]] bool state() const
	{
		return stable_state_;
	}

	bool state_changed()
	{
		const bool c = changed_;
		changed_ = false;
		return c;
	}

private:
	source& src_;
	bool last_raw_{false};
	bool stable_state_{false};
	bool changed_{false};
	uint32_t last_change_ms_{0};
};