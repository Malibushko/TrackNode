#include "common/common.h"
#include "libs/freertos/freertos_helpers.h"

namespace common
{

uint32_t get_tick()
{
	return xTaskGetTickCount();
}

void delay(uint32_t time_ms)
{
	vTaskDelay(pdMS_TO_TICKS(time_ms));
}

}