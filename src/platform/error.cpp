#include "platform/error.h"
#include "platform/hal.h"

[[noreturn]] void error_handler()
{
	__disable_irq();

	while (true)
	{
		// Empty
	}
}
