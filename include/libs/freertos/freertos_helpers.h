#pragma once
#include "core/result.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "common/task_events.h"

inline result<TaskHandle_t> task_create(
	TaskFunction_t task_code,
	const char * const name,
	const configSTACK_DEPTH_TYPE stack_depth,
	void * const parameters,
	UBaseType_t priority)
{
	TaskHandle_t handle = nullptr;
	BaseType_t res = xTaskCreate(task_code, name, stack_depth, parameters, priority, &handle);

	if (res != pdPASS)
	{
		return fail_rtos(error_code::out_of_memory, res);
	}

	return handle;
}

inline void signal_task_event(TaskHandle_t task)
{
	if (task)
	{
		(void)xTaskNotifyGiveIndexed(task, task_event_notification_index);
	}
}

inline void signal_task_event_from_isr(TaskHandle_t task, BaseType_t *higher_priority_woken)
{
	if (task)
	{
		vTaskNotifyGiveIndexedFromISR(task, task_event_notification_index, higher_priority_woken);
	}
}

inline bool wait_task_event(TickType_t ticks)
{
	return ulTaskNotifyTakeIndexed(task_event_notification_index, pdTRUE, ticks) != 0;
}
