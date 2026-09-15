#include "platform/error.h"
#include "platform/hal.h"

#define SCB_CFSR   (*(volatile uint32_t*)0xE000ED28)
#define SCB_HFSR   (*(volatile uint32_t*)0xE000ED2C)
#define SCB_MMFAR  (*(volatile uint32_t*)0xE000ED34)
#define SCB_BFAR   (*(volatile uint32_t*)0xE000ED38)

void error_handler(error_code error_code)
{
	switch (error_code)
	{
		case error_code::invalid_config:
		case error_code::not_initialized:
		case error_code::already_initialized:
		{
			g_state = program_state::initialization_error;
			break;
		}

		case error_code::hal_failure:
		{
			g_state = program_state::hal_error;
			break;
		}

		default:
		{
			g_state = program_state::runtime_error;
			break;
		}
	}
}

void panic()
{
	__disable_irq();

	while (true)
	{
		// Empty
	}
}

extern "C" void HardFault_Handler(void)
{
	__asm volatile
	(
		"tst lr, #4                \n"
		"ite eq                    \n"
		"mrseq r0, msp             \n"
		"mrsne r0, psp             \n"
		"b HardFault_Handler_C     \n"
	);
}

extern "C" void HardFault_Handler_C(uint32_t *stack)
{
	volatile uint32_t r0  = stack[0];
	volatile uint32_t r1  = stack[1];
	volatile uint32_t r2  = stack[2];
	volatile uint32_t r3  = stack[3];
	volatile uint32_t r12 = stack[4];
	volatile uint32_t lr  = stack[5];
	volatile uint32_t pc  = stack[6];   // <-- вот он, реальный адрес краша
	volatile uint32_t psr = stack[7];

	volatile uint32_t cfsr  = SCB_CFSR;
	volatile uint32_t hfsr  = SCB_HFSR;
	volatile uint32_t mmfar = SCB_MMFAR;
	volatile uint32_t bfar  = SCB_BFAR;

	(void)r0; (void)r1; (void)r2; (void)r3; (void)r12; (void)lr;
	(void)pc; (void)psr; (void)cfsr; (void)hfsr; (void)mmfar; (void)bfar;

	while (true)
	{

	}
}