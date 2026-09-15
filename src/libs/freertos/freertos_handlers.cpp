#include "libs/freertos/freertos_handlers.h"
#include "platform/hal.h"

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
	(void)xTask;
	(void)pcTaskName;
	__disable_irq();
	for (;;) {}   // повесимся тут для отладки — увидишь по имени таска, кто переполнился
}
