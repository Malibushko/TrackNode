#pragma once

#include "drivers/gpio.h"
#include "infrastructure/service_status.h"

class status_indicator
{
public:
	explicit status_indicator(gpio &output) : output_(output)
	{
	}

	void start_blink(uint8_t count, uint32_t now)
	{
		if (state_ != program_state::ok || count == 0 || busy())
		{
			return;
		}

		transitions_left_ = static_cast<uint16_t>(count) * 2;
		phase_ = phase::prepare;
		changed_at_ = now;
		output_high_ = false;
	}

	[[nodiscard]] bool busy() const
	{
		return phase_ != phase::idle;
	}

	status update(uint32_t now, program_state state)
	{
		if (state != state_)
		{
			state_ = state;
			phase_ = phase::idle;
			output_high_ = state == program_state::ok;

			if (state != program_state::ok)
			{
				start_error_pattern(now);
			}
		}

		if (phase_ != phase::idle)
		{
			const uint32_t duration = phase_ == phase::gap ? 1000u : 150u;

			if (static_cast<uint32_t>(now - changed_at_) >= duration)
			{
				changed_at_ = now;

				if (phase_ == phase::gap)
				{
					start_error_pattern(now);
				}
				else if (transitions_left_ != 0)
				{
					output_high_ = !output_high_;
					--transitions_left_;
					phase_ = phase::blinking;
				}
				else if (state_ == program_state::ok)
				{
					phase_ = phase::idle;
					output_high_ = true;
				}
				else
				{
					phase_ = phase::gap;
				}
			}
		}

		if (!written_ || last_output_ != output_high_)
		{
			auto write_status = output_.write(output_high_ ? gpio::state::set : gpio::state::reset);

			if (!write_status)
			{
				return write_status;
			}

			written_ = true;
			last_output_ = output_high_;
		}

		return {};
	}

private:
	enum class phase
	{
		idle,
		prepare,
		blinking,
		gap
	};

	void start_error_pattern(uint32_t now)
	{
		uint8_t count = 3;

		if (state_ == program_state::hal_error)
		{
			count = 1;
		}
		else if (state_ == program_state::initialization_error)
		{
			count = 2;
		}

		transitions_left_ = static_cast<uint16_t>(count) * 2 - 1;
		output_high_ = true;
		changed_at_ = now;
		phase_ = phase::blinking;
	}

	gpio &output_;
	program_state state_ = program_state::ok;
	phase phase_ = phase::idle;
	uint32_t changed_at_ = 0;
	uint16_t transitions_left_ = 0;
	bool output_high_ = true;
	bool written_ = false;
	bool last_output_ = false;
};
