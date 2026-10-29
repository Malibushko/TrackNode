#include "platform/power_stats.h"

#include "platform/hal.h"
#include "FreeRTOS.h"
#include "task.h"

#include <algorithm>

namespace
{
constexpr std::uint16_t first_irq_exception = 16;

power_profile profile;
std::uint32_t sleep_started_at_cycle = 0;

//////////////////////////////////////////////////////////////////////////

std::uint16_t pending_exception()
{
	return static_cast<std::uint16_t>((SCB->ICSR & SCB_ICSR_VECTPENDING_Msk) >> SCB_ICSR_VECTPENDING_Pos);
}

//////////////////////////////////////////////////////////////////////////

constexpr std::uint16_t irq_exception(IRQn_Type irq)
{
	return static_cast<std::uint16_t>(first_irq_exception + irq);
}

//////////////////////////////////////////////////////////////////////////

bool debug_clocks_kept_in_low_power()
{
	constexpr auto low_power_debug_bits = DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY;
	return (DBGMCU->CR & low_power_debug_bits) != 0;
}

//////////////////////////////////////////////////////////////////////////
}

//////////////////////////////////////////////////////////////////////////

extern "C" void track_node_power_task_switched_in(void *task, const char *name)
{
	profile.record_task_switch(task, name);
}

//////////////////////////////////////////////////////////////////////////

extern "C" void track_node_power_before_sleep(void)
{
	sleep_started_at_cycle = DWT->CYCCNT;
	profile.record_sleep();
}

//////////////////////////////////////////////////////////////////////////

extern "C" void track_node_power_after_sleep(void)
{
	profile.record_slept_cycles(DWT->CYCCNT - sleep_started_at_cycle);
	profile.record_wake(pending_exception());
}

//////////////////////////////////////////////////////////////////////////

namespace power_stats
{
void start()
{
	if constexpr (!enabled)
	{
		return;
	}

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

//////////////////////////////////////////////////////////////////////////

void take_report(power_report &out)
{
	taskENTER_CRITICAL();
	profile.take_window(pdTICKS_TO_MS(xTaskGetTickCount()), out.window);
	taskEXIT_CRITICAL();

	out.core_clock_hz = SystemCoreClock;
	out.tickless = configUSE_TICKLESS_IDLE != 0;
	out.debug_clocks_in_low_power = debug_clocks_kept_in_low_power();
}

//////////////////////////////////////////////////////////////////////////

void record_stop(std::uint32_t slept_ms)
{
	if constexpr (!enabled)
	{
		return;
	}

	const auto core_cycles_per_ms = SystemCoreClock / 1000u;
	const auto slept_cycles = std::min<std::uint64_t>(static_cast<std::uint64_t>(slept_ms) * core_cycles_per_ms, UINT32_MAX);
	profile.record_sleep();
	profile.record_stop();
	profile.record_slept_cycles(static_cast<std::uint32_t>(slept_cycles));
	profile.record_wake(pending_exception());
}

//////////////////////////////////////////////////////////////////////////

const char *exception_name(std::uint16_t exception)
{
	switch (exception)
	{
		case 0:
			return "none";
		case 11:
			return "SVCall";
		case 14:
			return "PendSV";
		case 15:
			return "SysTick";
		case irq_exception(EXTI0_IRQn):
			return "EXTI0";
		case irq_exception(EXTI9_5_IRQn):
			return "EXTI9_5";
		case irq_exception(EXTI15_10_IRQn):
			return "EXTI15_10";
		case irq_exception(USART1_IRQn):
			return "USART1";
		case irq_exception(USART2_IRQn):
			return "USART2";
		case irq_exception(DMA2_Stream5_IRQn):
			return "USART1_RXDMA";
		case irq_exception(DMA2_Stream7_IRQn):
			return "USART1_TXDMA";
		case irq_exception(DMA1_Stream5_IRQn):
			return "USART2_RXDMA";
		case irq_exception(DMA1_Stream6_IRQn):
			return "USART2_TXDMA";
		case irq_exception(OTG_FS_IRQn):
			return "OTG_FS";
		case irq_exception(OTG_FS_WKUP_IRQn):
			return "OTG_FS_WKUP";
		case irq_exception(I2C1_EV_IRQn):
			return "I2C1_EV";
		case irq_exception(SPI1_IRQn):
			return "SPI1";
		case irq_exception(RTC_WKUP_IRQn):
			return "RTC_WKUP";
		default:
			return nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////
}
