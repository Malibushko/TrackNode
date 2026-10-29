#pragma once
#include "core/result.h"
#include "stream_buffer.h"

inline unsigned fake_task_events = 0;
inline void signal_task_event_from_isr(TaskHandle_t task, BaseType_t *)
{
	if (task)
	{
		++fake_task_events;
	}
}
