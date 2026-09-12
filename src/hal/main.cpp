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

    InitGPIO();
    InitUART(); // TODO: handle error
    InitADC();

    std::setvbuf(stdout, nullptr, _IONBF, 0);

    uint32_t adcValue = 0;
    float voltage = 0.0f;

    while (true)
    {
        HAL_ADC_Start(&hadc1);

        if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
        {
            adcValue = HAL_ADC_GetValue(&hadc1);
            voltage = (static_cast<float>(adcValue) * 3.36f) / 4095.f;

            std::printf("voltage=%f V\r\n", voltage);
        }

        HAL_ADC_Stop(&hadc1);

        HAL_Delay(1000);
    }
}