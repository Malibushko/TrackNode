#pragma once

#ifdef PERF_MEASURING

#include "common/scope_guard.h"
#include "FreeRTOS.h"
#include "task.h"

#define PERF_SCOPE(logger, name) \
	BLOCK_GUARD([perf_logger = &(logger), perf_start = xTaskGetTickCount(), \
	             perf_name = (name)] { \
		const TickType_t elapsed = xTaskGetTickCount() - perf_start; \
		(void)perf_logger->write("PERF %s: %lu ms\r\n", perf_name, \
		                         static_cast<unsigned long>(pdTICKS_TO_MS(elapsed))); \
	})

#else

#define PERF_SCOPE(logger, name) do {} while (false)

#endif
