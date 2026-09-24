#include "libs/freertos/freertos_handlers.h"

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
	(void)xTask;
	(void)pcTaskName;
	__disable_irq();
	for (;;) {}
}

//////////////////////////////////////////////////////////////////////////
