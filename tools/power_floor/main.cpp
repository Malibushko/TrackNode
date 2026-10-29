#include "stm32f4xx_hal.h"

#include <cstdint>

namespace
{
enum class floor_mode : std::uint32_t
{
	run,
	sleep,
	stop,
	standby,
	count
};

constexpr std::uint32_t mode_signature = 0x504F5700u;
constexpr std::uint32_t mode_signature_mask = 0xFFFFFF00u;
constexpr std::uint32_t blink_on_ms = 150;
constexpr std::uint32_t blink_off_ms = 350;
constexpr std::uint32_t settle_ms = 1500;

GPIO_TypeDef *const led_port = GPIOC;
constexpr std::uint16_t led_pin = GPIO_PIN_13;

//////////////////////////////////////////////////////////////////////////

void configure_clock()
{
	RCC_OscInitTypeDef oscillator_config{};
	RCC_ClkInitTypeDef clock_config{};

	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

	oscillator_config.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	oscillator_config.HSEState = RCC_HSE_ON;
	oscillator_config.PLL.PLLState = RCC_PLL_ON;
	oscillator_config.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	oscillator_config.PLL.PLLM = 25;
	oscillator_config.PLL.PLLN = 288;
	oscillator_config.PLL.PLLP = RCC_PLLP_DIV4;
	oscillator_config.PLL.PLLQ = 6;
	if (HAL_RCC_OscConfig(&oscillator_config) != HAL_OK)
	{
		return;
	}

	clock_config.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	clock_config.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	clock_config.AHBCLKDivider = RCC_SYSCLK_DIV1;
	clock_config.APB1CLKDivider = RCC_HCLK_DIV2;
	clock_config.APB2CLKDivider = RCC_HCLK_DIV1;
	(void)HAL_RCC_ClockConfig(&clock_config, FLASH_LATENCY_2);
}

//////////////////////////////////////////////////////////////////////////

floor_mode advance_mode()
{
	HAL_PWR_EnableBkUpAccess();
	const auto stored = RTC->BKP0R;
	const auto stored_by_this_firmware = (stored & mode_signature_mask) == mode_signature;
	const auto index = stored_by_this_firmware ? (stored & ~mode_signature_mask) % static_cast<std::uint32_t>(floor_mode::count) : 0u;
	const auto next = (index + 1) % static_cast<std::uint32_t>(floor_mode::count);
	RTC->BKP0R = mode_signature | next;
	HAL_PWR_DisableBkUpAccess();
	return static_cast<floor_mode>(index);
}

//////////////////////////////////////////////////////////////////////////

void blink_mode_number(floor_mode mode)
{
	__HAL_RCC_GPIOC_CLK_ENABLE();
	GPIO_InitTypeDef led{};
	led.Pin = led_pin;
	led.Mode = GPIO_MODE_OUTPUT_PP;
	led.Pull = GPIO_NOPULL;
	led.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(led_port, &led);

	const auto blinks = static_cast<std::uint32_t>(mode) + 1;
	for (std::uint32_t index = 0; index < blinks; ++index)
	{
		HAL_GPIO_WritePin(led_port, led_pin, GPIO_PIN_RESET);
		HAL_Delay(blink_on_ms);
		HAL_GPIO_WritePin(led_port, led_pin, GPIO_PIN_SET);
		HAL_Delay(blink_off_ms);
	}
	HAL_Delay(settle_ms);
}

//////////////////////////////////////////////////////////////////////////

void park_pins()
{
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();

	GPIO_InitTypeDef analog{};
	analog.Mode = GPIO_MODE_ANALOG;
	analog.Pull = GPIO_NOPULL;

	analog.Pin = GPIO_PIN_All & ~(GPIO_PIN_13 | GPIO_PIN_14);
	HAL_GPIO_Init(GPIOA, &analog);
	analog.Pin = GPIO_PIN_All;
	HAL_GPIO_Init(GPIOB, &analog);
	HAL_GPIO_Init(GPIOC, &analog);
	analog.Pin = GPIO_PIN_0 | GPIO_PIN_1;
	HAL_GPIO_Init(GPIOH, &analog);

	__HAL_RCC_GPIOA_CLK_DISABLE();
	__HAL_RCC_GPIOB_CLK_DISABLE();
	__HAL_RCC_GPIOC_CLK_DISABLE();
	__HAL_RCC_GPIOH_CLK_DISABLE();
}

//////////////////////////////////////////////////////////////////////////

[[noreturn]] void run_forever()
{
	for (;;)
	{
		__NOP();
	}
}

//////////////////////////////////////////////////////////////////////////

[[noreturn]] void sleep_forever()
{
	HAL_SuspendTick();
	for (;;)
	{
		HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
	}
}

//////////////////////////////////////////////////////////////////////////

[[noreturn]] void stop_forever()
{
	HAL_SuspendTick();
	HAL_PWREx_EnableFlashPowerDown();
	HAL_PWREx_EnableLowRegulatorLowVoltage();
	for (;;)
	{
		HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
	}
}

//////////////////////////////////////////////////////////////////////////

[[noreturn]] void standby_forever()
{
	HAL_SuspendTick();
	HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN1);
	__HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
	for (;;)
	{
		HAL_PWR_EnterSTANDBYMode();
	}
}

//////////////////////////////////////////////////////////////////////////

[[noreturn]] void enter(floor_mode mode)
{
	switch (mode)
	{
		case floor_mode::run:
			run_forever();
		case floor_mode::sleep:
			sleep_forever();
		case floor_mode::stop:
			stop_forever();
		case floor_mode::standby:
		case floor_mode::count:
			standby_forever();
	}
	standby_forever();
}

//////////////////////////////////////////////////////////////////////////
}

//////////////////////////////////////////////////////////////////////////

extern "C" void SysTick_Handler()
{
	HAL_IncTick();
}

//////////////////////////////////////////////////////////////////////////

int main()
{
	DBGMCU->CR = 0;
	HAL_Init();
	configure_clock();

	const auto mode = advance_mode();
	blink_mode_number(mode);
	park_pins();
	enter(mode);
}

//////////////////////////////////////////////////////////////////////////
