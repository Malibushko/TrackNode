#include "app/track_node.h"
#include "board/clock.h"
#include "drivers/devices/bmp280.h"
#include "platform/stdio.h"
#include <cstdio>

track_node::track_node(board &hardware)
    : hardware_(hardware),
      diagnostic_led_(gpio_config{
          .pin = { GPIOC, GPIO_PIN_13 }, .mode = GPIO_MODE_OUTPUT_PP, .initial_high = true, .active_low = true }),
      indicator_(diagnostic_led_),
	  console_input_(hardware.get<uart>("console")),
	  protocol_(console_input_)
{
}

status track_node::init()
{
	if (attempted_)
	{
		return fail(component::board, error_code::already_initialized);
	}

	attempted_ = true;
	auto init_status = from_hal(component::board, HAL_Init())
	                       .and_then(
	                           [this]
	                           {
		                           return diagnostic_led_.init();
	                           });

	if (!init_status)
	{
		set_error(init_status.error(), true);
		return init_status;
	}

	diagnostic_ready_ = true;
	init_status = init_system_clock().and_then(
	    [this]
	    {
		    return hardware_.init();
	    });

	if (!init_status)
	{
		set_error(init_status.error(), true);
		return init_status;
	}

	set_stdout_uart(&hardware_.get<uart>("console"));
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	state_ = program_state::ok;
	return {};
}

void track_node::set_error(error failure, bool during_init)
{
	last_error_ = failure;
	state_ = during_init ? program_state::initialization_error : program_state::runtime_error;

	if (failure.code == error_code::hal_failure)
	{
		state_ = program_state::hal_error;
	}
}

void track_node::update(uint32_t now)
{
	if (!diagnostic_ready_)
	{
		return;
	}

	if (state_ == program_state::ok)
	{
		protocol_.consume(32);

		if (console_input_.last_error())
		{
			set_error(*console_input_.last_error(), false);
		}
		else
		{

		}
	}

	auto indicator_status = indicator_.update(now, state_);

	if (!indicator_status)
	{
		set_error(indicator_status.error(), false);
		diagnostic_ready_ = false;
	}
}
