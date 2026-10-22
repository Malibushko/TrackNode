#pragma once

#include "FreeRTOS.h"

#include <cstdint>

using SemaphoreHandle_t = int *;

inline constexpr TickType_t portMAX_DELAY = UINT32_MAX;

inline SemaphoreHandle_t xSemaphoreCreateMutex()
{
	return new int{0};
}

inline void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
	delete semaphore;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t)
{
	return pdPASS;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t)
{
	return pdPASS;
}
