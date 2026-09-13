#include "common/common.h"

#include <stm32f4xx_hal.h>

namespace common
{

void delay(uint32_t time_ms)
{
	HAL_Delay(time_ms);
}

}