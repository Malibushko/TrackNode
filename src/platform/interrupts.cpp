#include "platform/hal.h"

extern "C" void SysTick_Handler()
{
	HAL_IncTick();
}
