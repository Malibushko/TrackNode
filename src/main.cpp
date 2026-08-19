#include <cstdint>
#include <stm32f1xx_hal.h>

volatile uint32_t boot_ok = 0;

int main()
{
    HAL_Init();
    
    boot_ok = 0xDEADBEEF;

    while (true)
    {

    }
}