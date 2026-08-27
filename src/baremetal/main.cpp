#include <cstdint>

static constexpr std::size_t RCC_BASE = 0x40023800;
static constexpr std::size_t RCC_AHB1 = RCC_BASE + 0x30;

static constexpr std::size_t GPIO_C_MASK = (1 << 2);
static constexpr std::size_t GPIOC = 0x40020800;
static constexpr std::size_t GPIOC_MODER = GPIOC + 0x0;

static constexpr std::size_t GPIO_ODR = GPIOC + 0x14;
static const auto GPIOC_ODR_PC13_MASK = (1u << 13);

static constexpr std::size_t SYST_CSR  = 0xE000E010;
static constexpr std::size_t SYST_RVR  = 0xE000E014;
static constexpr std::size_t SYST_CVR  = 0xE000E018;

static constexpr uint32_t SYST_ENABLE    = 1u << 0;
static constexpr uint32_t SYST_CLKSOURCE = 1u << 2;
static constexpr uint32_t SYST_COUNTFLAG = 1u << 16;

static constexpr uint32_t CPU_FREQ_HZ = 16'000'000;
static constexpr uint32_t TICK_HZ     = 1000;

static void delay_ms(uint32_t ms)
{
    auto* csr = reinterpret_cast<volatile uint32_t*>(SYST_CSR);
    auto* rvr = reinterpret_cast<volatile uint32_t*>(SYST_RVR);
    auto* cvr = reinterpret_cast<volatile uint32_t*>(SYST_CVR);

    *rvr = CPU_FREQ_HZ / TICK_HZ - 1;
    *cvr = 0;

    *csr = SYST_ENABLE | SYST_CLKSOURCE;

    for (uint32_t i = 0; i < ms; ++i)
    {
        while ((*csr & SYST_COUNTFLAG) == 0)
        {
        }
    }

    *csr = 0;
}

int main()
{
    volatile uint32_t * rcc_ahb1en = reinterpret_cast<volatile uint32_t*>(RCC_AHB1);
    *rcc_ahb1en |= GPIO_C_MASK;

    volatile uint32_t* gpioc_moder = reinterpret_cast<volatile uint32_t*>(GPIOC_MODER);
    *gpioc_moder &= ~(1u << 27);
    *gpioc_moder |= (1u << 26);

    bool flip = true;
    volatile uint32_t* gpio_odr = reinterpret_cast<volatile uint32_t*>(GPIO_ODR);

    while (true)
    {
        if (flip)
        {
            *gpio_odr &= ~GPIOC_ODR_PC13_MASK;
        } else
        {
            *gpio_odr |= GPIOC_ODR_PC13_MASK;
        }

        flip = !flip;

        delay_ms(500);
    }
}