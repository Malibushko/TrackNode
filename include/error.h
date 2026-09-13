#pragma once
#include "hal.h"

[[noreturn]] void Error_Handler()
{
    __disable_irq();
    while (true) {}
}
