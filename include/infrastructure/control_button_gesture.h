#pragma once

#include <cstdint>
#include <optional>
#include <span>

struct control_button_gesture
{
	enum class kind
	{
		hold_threshold_reached,
		released
	};

	kind type;
	std::uint32_t duration_ms;
};

class control_button_gesture_recognizer
{
public:
	control_button_gesture_recognizer(std::uint32_t debounce_ms, std::span<const std::uint32_t> hold_thresholds_ms)
		: debounce_ms_(debounce_ms), hold_thresholds_ms_(hold_thresholds_ms) {}

	std::optional<control_button_gesture> update(bool pressed, std::uint32_t now)
	{
		if (pressed != candidate_)
		{
			candidate_ = pressed;
			changed_at_ = now;
		}
		const auto debounced_state_changed = stable_ != candidate_ && now - changed_at_ >= debounce_ms_;
		if (debounced_state_changed)
		{
			stable_ = candidate_;
			if (stable_)
			{
				pressed_at_ = now;
				next_threshold_ = 0;
			}
			else
			{
				return control_button_gesture{
					.type = control_button_gesture::kind::released,
					.duration_ms = changed_at_ - pressed_at_
				};
			}
		}
		const auto next_hold_threshold_reached = stable_ && pressed && next_threshold_ < hold_thresholds_ms_.size() &&
			now - pressed_at_ >= hold_thresholds_ms_[next_threshold_];
		if (next_hold_threshold_reached)
		{
			return control_button_gesture{
				.type = control_button_gesture::kind::hold_threshold_reached,
				.duration_ms = hold_thresholds_ms_[next_threshold_++]
			};
		}
		return std::nullopt;
	}

	[[nodiscard]] bool waiting_for_edge() const
	{
		const auto debounce_settled = candidate_ == stable_;
		const auto no_hold_threshold_ahead = !stable_ || next_threshold_ >= hold_thresholds_ms_.size();
		return debounce_settled && no_hold_threshold_ahead;
	}

private:
	std::uint32_t debounce_ms_;
	std::span<const std::uint32_t> hold_thresholds_ms_;

	std::size_t next_threshold_ = 0;
	std::uint32_t changed_at_ = 0;
	std::uint32_t pressed_at_ = 0;
	bool candidate_ = false;
	bool stable_ = false;
};
