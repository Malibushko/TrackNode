#include "platform/low_power.h"

#include "board/clock.h"
#include "platform/hal.h"
#include "platform/power_stats.h"
#include "task.h"

#include <algorithm>
#include <array>
#include <bit>

extern "C" void vPortSuppressTicksAndSleep(TickType_t expected_idle_ticks);

namespace
{
constexpr std::uint32_t rtc_async_prescaler = 7;
constexpr std::uint32_t rtc_sync_prescaler = 4095;
constexpr std::uint32_t subsecond_rate_hz = 4096;
constexpr std::uint32_t seconds_per_day = 86400;
constexpr std::uint32_t wakeup_timer_hz = 2048;
constexpr std::uint32_t wakeup_timer_max_counts = 0xFFFF;
constexpr std::uint32_t lse_start_timeout_ms = 3000;
constexpr std::uint32_t register_wait_limit = 100000;
constexpr std::uint32_t stop_exit_latency_ms = 3;
constexpr TickType_t minimum_stop_ticks = pdMS_TO_TICKS(5);
constexpr std::uint32_t wake_irq_priority = 6;
constexpr std::size_t veto_capacity = 4;
constexpr std::size_t wake_pin_capacity = 2;

struct veto_entry
{
	low_power::stop_veto veto = nullptr;
	const void *context = nullptr;
};

std::array<veto_entry, veto_capacity> vetoes{};
std::size_t veto_count = 0;
std::array<pin, wake_pin_capacity> wake_pins{};
std::size_t wake_pin_count = 0;
bool rtc_ready = false;

//////////////////////////////////////////////////////////////////////////

template <typename predicate_t>
bool wait_until(predicate_t &&condition)
{
	for (std::uint32_t attempt = 0; attempt < register_wait_limit; ++attempt)
	{
		if (condition())
		{
			return true;
		}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////

void unlock_rtc()
{
	RTC->WPR = 0xCA;
	RTC->WPR = 0x53;
}

//////////////////////////////////////////////////////////////////////////

void lock_rtc()
{
	RTC->WPR = 0xFF;
}

//////////////////////////////////////////////////////////////////////////

void clear_wakeup_flags()
{
	RTC->ISR = (~(RTC_ISR_WUTF | RTC_ISR_INIT) & 0x0000FFFFU) | (RTC->ISR & RTC_ISR_INIT);
	EXTI->PR = EXTI_PR_PR22;
}

//////////////////////////////////////////////////////////////////////////

status start_lse()
{
	__HAL_RCC_PWR_CLK_ENABLE();
	PWR->CR |= PWR_CR_DBP;
	constexpr auto rtc_on_lse = RCC_BDCR_RTCEN | RCC_BDCR_RTCSEL_0 | RCC_BDCR_LSERDY;
	const auto rtc_already_on_lse = (RCC->BDCR & (RCC_BDCR_RTCEN | RCC_BDCR_RTCSEL | RCC_BDCR_LSERDY)) == rtc_on_lse;
	if (rtc_already_on_lse)
	{
		return {};
	}

	RCC->BDCR |= RCC_BDCR_BDRST;
	RCC->BDCR &= ~RCC_BDCR_BDRST;
	RCC->BDCR |= RCC_BDCR_LSEMOD;
	RCC->BDCR |= RCC_BDCR_LSEON;
	const auto started = HAL_GetTick();
	while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0)
	{
		if (HAL_GetTick() - started > lse_start_timeout_ms)
		{
			RCC->BDCR &= ~RCC_BDCR_LSEON;
			return fail(component::clock, error_code::timeout_error);
		}
	}
	RCC->BDCR |= RCC_BDCR_RTCSEL_0;
	RCC->BDCR |= RCC_BDCR_RTCEN;
	return {};
}

//////////////////////////////////////////////////////////////////////////

status configure_rtc()
{
	unlock_rtc();
	RTC->ISR |= RTC_ISR_INIT;
	if (!wait_until([]{ return (RTC->ISR & RTC_ISR_INITF) != 0; }))
	{
		lock_rtc();
		return fail(component::clock, error_code::timeout_error);
	}
	RTC->PRER = rtc_sync_prescaler;
	RTC->PRER |= rtc_async_prescaler << RTC_PRER_PREDIV_A_Pos;
	RTC->TR = 0;
	RTC->CR |= RTC_CR_BYPSHAD;
	RTC->ISR &= ~RTC_ISR_INIT;

	RTC->CR &= ~RTC_CR_WUTE;
	if (!wait_until([]{ return (RTC->ISR & RTC_ISR_WUTWF) != 0; }))
	{
		lock_rtc();
		return fail(component::clock, error_code::timeout_error);
	}
	RTC->CR &= ~RTC_CR_WUCKSEL;
	RTC->CR |= RTC_CR_WUTIE;
	lock_rtc();
	clear_wakeup_flags();
	return {};
}

//////////////////////////////////////////////////////////////////////////

void enable_wakeup_interrupts()
{
	EXTI->IMR |= EXTI_IMR_MR22;
	EXTI->RTSR |= EXTI_RTSR_TR22;
	HAL_NVIC_SetPriority(RTC_WKUP_IRQn, wake_irq_priority, 0);
	HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);
}

//////////////////////////////////////////////////////////////////////////

IRQn_Type exti_irq(std::uint32_t line)
{
	switch (line)
	{
		case 0:
			return EXTI0_IRQn;
		case 1:
			return EXTI1_IRQn;
		case 2:
			return EXTI2_IRQn;
		case 3:
			return EXTI3_IRQn;
		case 4:
			return EXTI4_IRQn;
		default:
			return line <= 9 ? EXTI9_5_IRQn : EXTI15_10_IRQn;
	}
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t port_index(GPIO_TypeDef *port)
{
	if (port == GPIOB)
	{
		return 1;
	}
	if (port == GPIOC)
	{
		return 2;
	}
	if (port == GPIOH)
	{
		return 7;
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t bcd_value(std::uint32_t tens, std::uint32_t units)
{
	return tens * 10 + units;
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t read_rtc_subticks()
{
	std::uint32_t subseconds = 0;
	std::uint32_t time = 0;
	do
	{
		subseconds = RTC->SSR;
		time = RTC->TR;
	} while (subseconds != RTC->SSR);

	const auto hours = bcd_value((time & RTC_TR_HT) >> RTC_TR_HT_Pos, (time & RTC_TR_HU) >> RTC_TR_HU_Pos);
	const auto minutes = bcd_value((time & RTC_TR_MNT) >> RTC_TR_MNT_Pos, (time & RTC_TR_MNU) >> RTC_TR_MNU_Pos);
	const auto seconds = bcd_value((time & RTC_TR_ST) >> RTC_TR_ST_Pos, (time & RTC_TR_SU) >> RTC_TR_SU_Pos);
	const auto second_of_day = (hours * 60 + minutes) * 60 + seconds;
	return second_of_day * subsecond_rate_hz + (rtc_sync_prescaler - subseconds);
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t elapsed_ms(std::uint32_t started_at, std::uint32_t finished_at)
{
	constexpr std::uint32_t subticks_per_day = seconds_per_day * subsecond_rate_hz;
	const auto elapsed = (finished_at + subticks_per_day - started_at) % subticks_per_day;
	return static_cast<std::uint32_t>(static_cast<std::uint64_t>(elapsed) * 1000u / subsecond_rate_hz);
}

//////////////////////////////////////////////////////////////////////////

void arm_wakeup_timer(std::uint32_t sleep_ms)
{
	const auto counts = std::clamp<std::uint64_t>(static_cast<std::uint64_t>(sleep_ms) * wakeup_timer_hz / 1000u,
		1u, wakeup_timer_max_counts);
	unlock_rtc();
	RTC->CR &= ~RTC_CR_WUTE;
	(void)wait_until([]{ return (RTC->ISR & RTC_ISR_WUTWF) != 0; });
	RTC->WUTR = static_cast<std::uint32_t>(counts - 1);
	clear_wakeup_flags();
	RTC->CR |= RTC_CR_WUTE;
	lock_rtc();
}

//////////////////////////////////////////////////////////////////////////

void disarm_wakeup_timer()
{
	unlock_rtc();
	RTC->CR &= ~RTC_CR_WUTE;
	lock_rtc();
	clear_wakeup_flags();
}

//////////////////////////////////////////////////////////////////////////

void arm_wake_pins()
{
	for (std::size_t index = 0; index < wake_pin_count; ++index)
	{
		const auto &input = wake_pins[index];
		const auto line = static_cast<std::uint32_t>(std::countr_zero(input.mask));
		const auto shift = (line % 4) * 4;
		auto &selector = SYSCFG->EXTICR[line / 4];
		selector = (selector & ~(0xFu << shift)) | (port_index(input.port) << shift);
		EXTI->FTSR |= input.mask;
		EXTI->PR = input.mask;
		EXTI->IMR |= input.mask;
	}
}

//////////////////////////////////////////////////////////////////////////

void disarm_wake_pins()
{
	for (std::size_t index = 0; index < wake_pin_count; ++index)
	{
		const auto mask = wake_pins[index].mask;
		EXTI->IMR &= ~static_cast<std::uint32_t>(mask);
		EXTI->FTSR &= ~static_cast<std::uint32_t>(mask);
		EXTI->PR = mask;
	}
}

//////////////////////////////////////////////////////////////////////////

bool stop_vetoed()
{
	for (std::size_t index = 0; index < veto_count; ++index)
	{
		if (vetoes[index].veto(vetoes[index].context))
		{
			return true;
		}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////

void enter_stop()
{
	PWR->CR &= ~PWR_CR_PDDS;
	PWR->CR |= PWR_CR_LPDS | PWR_CR_FPDS | PWR_CR_LPLVDS;
	SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
	__DSB();
	__WFI();
	__ISB();
	SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
}

//////////////////////////////////////////////////////////////////////////

void restart_tick()
{
	SysTick->VAL = 0;
	SysTick->LOAD = SystemCoreClock / configTICK_RATE_HZ - 1;
	SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
}

//////////////////////////////////////////////////////////////////////////

std::uint32_t wakeup_after_ms(TickType_t expected_idle_ticks)
{
	const auto expected_ms = static_cast<std::uint32_t>(std::min<std::uint64_t>(pdTICKS_TO_MS(expected_idle_ticks),
		static_cast<std::uint64_t>(wakeup_timer_max_counts) * 1000u / wakeup_timer_hz));
	const auto exit_latency_fits = expected_ms > stop_exit_latency_ms * 2;
	return exit_latency_fits ? expected_ms - stop_exit_latency_ms : expected_ms;
}

//////////////////////////////////////////////////////////////////////////

#if configUSE_TICKLESS_IDLE
void sleep_in_stop(TickType_t expected_idle_ticks)
{
	SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
	const auto started_at = read_rtc_subticks();
	arm_wakeup_timer(wakeup_after_ms(expected_idle_ticks));
	arm_wake_pins();

	enter_stop();
	resume_system_clock();

	disarm_wake_pins();
	disarm_wakeup_timer();
	const auto slept_ms = elapsed_ms(started_at, read_rtc_subticks());
	power_stats::record_stop(slept_ms);
	const auto slept_ticks = std::min<TickType_t>(pdMS_TO_TICKS(slept_ms), expected_idle_ticks);
	vTaskStepTick(slept_ticks);
	restart_tick();
}
#endif

//////////////////////////////////////////////////////////////////////////
}

//////////////////////////////////////////////////////////////////////////

namespace low_power
{
status start()
{
	if constexpr (!enabled)
	{
		return {};
	}
	if (rtc_ready)
	{
		return fail(component::clock, error_code::already_initialized);
	}

	if (auto oscillator = start_lse().and_then(configure_rtc); !oscillator)
	{
		return oscillator;
	}
	enable_wakeup_interrupts();
	rtc_ready = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

status add_stop_veto(stop_veto veto, const void *context)
{
	if (!veto || veto_count == vetoes.size())
	{
		return fail(component::board, error_code::invalid_config);
	}

	vetoes[veto_count++] = veto_entry{
		.veto = veto,
		.context = context
	};
	return {};
}

//////////////////////////////////////////////////////////////////////////

status add_wake_pin(pin input)
{
	const auto single_pin = input.port && input.mask && (input.mask & (input.mask - 1u)) == 0;
	if (!single_pin || wake_pin_count == wake_pins.size())
	{
		return fail(component::board, error_code::invalid_config);
	}

	__HAL_RCC_SYSCFG_CLK_ENABLE();
	wake_pins[wake_pin_count++] = input;
	const auto irq = exti_irq(static_cast<std::uint32_t>(std::countr_zero(input.mask)));
	HAL_NVIC_SetPriority(irq, wake_irq_priority, 0);
	HAL_NVIC_EnableIRQ(irq);
	return {};
}

//////////////////////////////////////////////////////////////////////////

bool stop_available()
{
	return rtc_ready;
}
}

//////////////////////////////////////////////////////////////////////////

#if configUSE_TICKLESS_IDLE
extern "C" void track_node_suppress_ticks_and_sleep(TickType_t expected_idle_ticks)
{
	const auto long_enough_for_stop = expected_idle_ticks >= minimum_stop_ticks;
	if (!rtc_ready || !long_enough_for_stop || stop_vetoed())
	{
		vPortSuppressTicksAndSleep(expected_idle_ticks);
		return;
	}

	__disable_irq();
	__DSB();
	__ISB();
	const auto sleep_still_allowed = eTaskConfirmSleepModeStatus() != eAbortSleep && !stop_vetoed();
	if (sleep_still_allowed)
	{
		sleep_in_stop(expected_idle_ticks);
	}
	__enable_irq();
}
#endif

//////////////////////////////////////////////////////////////////////////

extern "C" void RTC_WKUP_IRQHandler()
{
	clear_wakeup_flags();
}

//////////////////////////////////////////////////////////////////////////
