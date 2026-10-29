#pragma once

#include "FreeRTOS.h"
#include "task.h"

class task_event_source
{
public:
	virtual void set_listener(TaskHandle_t task) = 0;

protected:
	~task_event_source() = default;
};
