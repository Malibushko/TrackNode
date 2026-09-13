#pragma once

#pragma once

#include "misc/tasks/status_blink_task.h"
#include "drivers/gpio.h"

enum class node_status
{
	ok,
	error,
	init_failure,
};

inline status_blink_task make_status_blinker(gpio &led, const node_status &status_ref)
{
	while (true)
	{
		const node_status current = status_ref;

		if (current == node_status::ok)
		{
			led.write(true);
			co_await status_blink_task::delay_ms{300};
			continue;
		}

		int blink_count = 0;
		switch (current)
		{
		case node_status::error:        blink_count = 2; break;
		case node_status::init_failure:  blink_count = 3; break;
		default:                         blink_count = 1; break;
		}

		for (int i = 0; i < blink_count; ++i)
		{
			led.write(true);
			co_await status_blink_task::delay_ms{150};
			led.write(false);
			co_await status_blink_task::delay_ms{150};
		}

		co_await status_blink_task::delay_ms{1000};
	}
}