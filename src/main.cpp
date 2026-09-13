#include <cstdint>
#include <cstdio>

// DO NOT REMOVE IT
#include "clock.h"

#include "gpio.h"
#include "uart.h"
#include "adc.h"

int main()
{
    HAL_Init();

    InitClock();
    InitGPIO();
    InitUART(); // TODO: handle error
    InitADC();

    std::setvbuf(stdout, nullptr, _IONBF, 0);

    while (true)
    {
    }
}