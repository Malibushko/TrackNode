#include <cstdint>
#include <cstdio>
#include <cerrno>
#include "stm32f4xx_hal.h"

extern "C" void SysTick_Handler()
{
    HAL_IncTick();
}

UART_HandleTypeDef UART_Handler;

void InitGPIO()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct{};

    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;

    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

bool InitUART()
{
    __HAL_RCC_USART1_CLK_ENABLE();

    UART_Handler.Instance = USART1;
    UART_Handler.Init.BaudRate = 115200;
    UART_Handler.Init.WordLength = UART_WORDLENGTH_8B;
    UART_Handler.Init.StopBits = UART_STOPBITS_1;
    UART_Handler.Init.Parity = UART_PARITY_NONE;
    UART_Handler.Init.Mode = UART_MODE_TX;
    UART_Handler.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    UART_Handler.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&UART_Handler) != HAL_OK)
    {
        return false;
    }

    return true;
}

extern "C" int _write(int fd, char* data, int len)
{
    if (fd != 1 && fd != 2) { // stdout и stderr
        errno = EBADF;
        return -1;
    }

    if (len < 0 || len > UINT16_MAX) {
        errno = EINVAL;
        return -1;
    }

    if (len == 0)
        return 0;

    auto status = HAL_UART_Transmit(
        &UART_Handler,
        reinterpret_cast<uint8_t*>(data),
        static_cast<uint16_t>(len),
        100);

    if (status != HAL_OK) {
        errno = EIO;
        return -1;
    }

    return len;
}

int main()
{
    HAL_Init();
    const volatile auto freq = HAL_RCC_GetPCLK2Freq();

    InitGPIO();
    InitUART(); // TODO: handle error

    std::setvbuf(stdout, nullptr, _IONBF, 0);

    int counter = 0;

    while (true)
    {
        std::printf("counter=%u\r\n", counter++);

        HAL_Delay(1000);
    }
}