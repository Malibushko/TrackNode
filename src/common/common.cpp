#include "common/common.h"
#include "libs/freertos/freertos_helpers.h"

namespace common
{

uint32_t get_tick()
{
	return xTaskGetTickCount();
}

uint32_t ticks_to_ms(uint32_t ticks)
{
	return pdTICKS_TO_MS(ticks);
}

uint32_t ms_to_ticks(uint32_t ms)
{
	return pdMS_TO_TICKS(ms);
}

//////////////////////////////////////////////////////////////////////////

void delay(uint32_t time_ms)
{
	vTaskDelay(pdMS_TO_TICKS(time_ms));
}

//////////////////////////////////////////////////////////////////////////

}
