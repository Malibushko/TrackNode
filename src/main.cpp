#include <cstdint>

volatile uint32_t boot_ok = 0;

int main()
{
    boot_ok = 0xDEADBEEF;

    while (true)
    {

    }
}