#pragma once
#include "io.h"

UART_HandleTypeDef UART_Handler;

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
