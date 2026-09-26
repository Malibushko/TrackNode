#pragma once
#include <cstdint>

namespace common
{
	uint32_t get_tick();
	uint32_t ticks_to_ms(uint32_t ticks);
	uint32_t ms_to_ticks(uint32_t ms);

	void delay(uint32_t time_ms);
}