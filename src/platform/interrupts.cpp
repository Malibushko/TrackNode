#include "platform/hal.h"
#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"

extern "C" void xPortSysTickHandler(void);

extern "C" void SysTick_Handler()
{
	HAL_IncTick();

	if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
	{
		xPortSysTickHandler();
	}
}
