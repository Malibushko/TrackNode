#pragma once

#ifdef FAULT_INJECTION_ENABLE

#include <cstdint>
#include <cstdlib>
#include "libs/freertos/freertos_helpers.h"

namespace fault
{
    inline void task_inject_random_delay(uint32_t min, uint32_t max)
    {
        std::srand(xTaskGetTickCount());
        vTaskDelay(rand() % (max - min + 1) + min);
    }
}

#endif
