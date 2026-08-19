#include <cstdint>

volatile uint32_t boot_ok = 0;

int main()
{
    boot_ok = 0xDEADBEEF;

    volatile int number = 1;

    for (int i = 0; i < 1000; i++)
    {
        number++;
    }

    number = 42;

    while (true)
    {
    }
}