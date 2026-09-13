#pragma once

#pragma once

#include "misc/tasks/status_blink_task.h"
#include "drivers/gpio.h"

inline status_blink_task make_status_blinker(gpio &led, const program_state &status_ref)
{
	while (true)
	{
		const program_state current = status_ref;

		if (current == program_state::ok)
		{
			led.write(gpio::state::set);
			co_await status_blink_task::delay_ms{300};
			continue;
		}

		int blink_count = 0;
		switch (current)
		{
			case program_state::hal_error:
			{
				blink_count = 1;
				break;
			}
			case program_state::initialization_error:
			{
				blink_count = 2;
				break;
			}
			case program_state::runtime_error:
			{
				blink_count = 3;
				break;
			}
			default:
			{
				blink_count = 4;
				break;
			}
		}

		for (int i = 0; i < blink_count; ++i)
		{
			led.write(gpio::state::set);
			co_await status_blink_task::delay_ms{150};
			led.write(gpio::state::reset);
			co_await status_blink_task::delay_ms{150};
		}

		co_await status_blink_task::delay_ms{1000};
	}
}