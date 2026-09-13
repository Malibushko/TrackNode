#include "platform/error.h"
#include "platform/hal.h"

void error_handler(error_code error_code)
{
	switch (error_code)
	{
		case error_code::invalid_config:
		case error_code::not_initialized:
		case error_code::already_initialized:
		{
			g_state = program_state::initialization_error;
			break;
		}

		case error_code::hal_failure:
		{
			g_state = program_state::hal_error;
			break;
		}
	}
}

void panic()
{
	__disable_irq();

	while (true)
	{
		// Empty
	}
}
