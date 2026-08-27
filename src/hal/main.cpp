#include <cstdint>
#include "stm32f4xx_hal.h"

extern "C" void SysTick_Handler()
{
    HAL_IncTick();
}

int main()
{
    HAL_Init();

    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct{};
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    while (true)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

        volatile uint32_t tick = HAL_GetTick();
        (void)tick;

        HAL_Delay(500);
    }
}