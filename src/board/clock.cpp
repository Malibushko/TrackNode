#include "board/clock.h"

status init_system_clock()
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

	auto osc_status = HAL_RCC_OscConfig(&oscillator_config);
	if (osc_status != HAL_OK)
	{
		return fail(component::clock, error_code::hal_failure, osc_status);
	}

	clock_config.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	clock_config.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	clock_config.AHBCLKDivider = RCC_SYSCLK_DIV1;
	clock_config.APB1CLKDivider = RCC_HCLK_DIV2;
	clock_config.APB2CLKDivider = RCC_HCLK_DIV1;

	auto clock_status = HAL_RCC_ClockConfig(&clock_config, FLASH_LATENCY_2);
	if (clock_status != HAL_OK)
	{
		return fail(component::clock, error_code::hal_failure, clock_status);
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////
