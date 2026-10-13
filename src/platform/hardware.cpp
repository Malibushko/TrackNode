#include "platform/hardware.h"
#include "platform/bootloader_request.h"
#include "platform/hal.h"
#include "drivers/usb_device.h"

#include <cstddef>

namespace
{
volatile bootloader_request request __attribute__((section(".noinit.bootloader_request"), used));
constexpr std::uint32_t system_memory = 0x1FFF0000;

[[gnu::naked, noreturn]] void jump_to_bootloader(std::uint32_t, std::uint32_t)
{
	asm volatile(
		"msr msp, r0\n"
		"movs r2, #0\n"
		"msr control, r2\n"
		"msr basepri, r2\n"
		"dsb\n"
		"isb\n"
		"cpsie i\n"
		"bx r1\n");
}

//////////////////////////////////////////////////////////////////////////

void try_enter_requested_bootloader()
{
	const bool software_reset = (RCC->CSR & RCC_CSR_SFTRSTF) != 0;
	if (!consume_bootloader_request(request, software_reset))
	{
		return;
	}
	__HAL_RCC_CLEAR_RESET_FLAGS();
	__disable_irq();
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;
	for (std::size_t index = 0; index < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); ++index)
	{
		NVIC->ICER[index] = 0xFFFFFFFF;
		NVIC->ICPR[index] = 0xFFFFFFFF;
	}
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
	__HAL_RCC_SYSCFG_CLK_ENABLE();
	__HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
	SCB->VTOR = system_memory;
	const auto *vectors = reinterpret_cast<const std::uint32_t *>(system_memory);
	jump_to_bootloader(vectors[0], vectors[1]);
}

//////////////////////////////////////////////////////////////////////////

}

void hardware::process_pending_requests()
{
	try_enter_requested_bootloader();
}

//////////////////////////////////////////////////////////////////////////

[[noreturn]] void hardware::request_bootloader()
{
	taskENTER_CRITICAL();
	if (auto *usb = usb_device::active())
	{
		usb->stop();
	}
	taskEXIT_CRITICAL();
	vTaskDelay(pdMS_TO_TICKS(100));
	__disable_irq();
	request.magic = bootloader_request_magic;
	request.inverse = ~bootloader_request_magic;
	__DSB();
	NVIC_SystemReset();
}
