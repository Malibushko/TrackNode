#pragma once
#include "core/result.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

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