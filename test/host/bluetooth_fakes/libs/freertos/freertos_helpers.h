#pragma once
#include "core/result.h"
#include <deque>
#include <vector>
#include <cstring>

using TaskHandle_t = void *;
using TaskFunction_t = void (*)(void *);
inline constexpr unsigned configMINIMAL_STACK_SIZE = 128;
inline constexpr unsigned tskIDLE_PRIORITY = 0;
inline constexpr unsigned configMAX_PRIORITIES = 5;
using configSTACK_DEPTH_TYPE = std::uint16_t;
using UBaseType_t = std::uint32_t;
inline unsigned fake_stack_depth = 0;
inline unsigned fake_priority = 0;
inline constexpr unsigned portTICK_PERIOD_MS = 1;
inline TickType_t fake_ticks = 0;
inline TaskFunction_t fake_task = nullptr;
inline void *fake_argument = nullptr;
inline bool fail_task_create = false;

struct fake_queue
{
	std::size_t capacity;
	std::size_t item_size;
	std::deque<std::vector<unsigned char>> items;
};
using QueueHandle_t = fake_queue *;
inline QueueHandle_t xQueueCreate(std::size_t capacity, std::size_t size)
{
	return new fake_queue{capacity, size, {}};
}
inline void vQueueDelete(QueueHandle_t queue)
{
	delete queue;
}
inline BaseType_t xQueueReset(QueueHandle_t queue)
{
	queue->items.clear();
	return pdPASS;
}
inline BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t)
{
	if (queue->items.size() == queue->capacity)
	{
		return pdFAIL;
	}

	const auto *bytes = static_cast<const unsigned char *>(item);
	queue->items.emplace_back(bytes, bytes + queue->item_size);
	return pdPASS;
}
inline BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item)
{
	queue->items.clear();
	return xQueueSend(queue, item, 0);
}
inline BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t)
{
	if (queue->items.empty())
	{
		return pdFAIL;
	}

	std::memcpy(item, queue->items.front().data(), queue->item_size);
	queue->items.pop_front();
	return pdPASS;
}
inline result<TaskHandle_t> task_create(TaskFunction_t function, const char *, unsigned stack_depth, void *argument, unsigned priority)
{
	if (fail_task_create)
	{
		return fail(component::rtos, error_code::out_of_memory);
	}

	fake_task = function;
	fake_stack_depth = stack_depth;
	fake_priority = priority;
	fake_argument = argument;
	return static_cast<void *>(&fake_ticks);
}
inline void vTaskDelete(TaskHandle_t) {}
inline TickType_t xTaskGetTickCount()
{
	return fake_ticks;
}
inline void vTaskDelay(TickType_t ticks)
{
	fake_ticks += ticks;
}

inline unsigned fake_task_events = 0;
inline void signal_task_event(TaskHandle_t task)
{
	if (task)
	{
		++fake_task_events;
	}
}
inline TaskHandle_t xTaskGetCurrentTaskHandle()
{
	return static_cast<void *>(&fake_ticks);
}
inline bool wait_task_event(TickType_t ticks)
{
	fake_ticks += ticks;
	return false;
}
inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue)
{
	return queue ? static_cast<UBaseType_t>(queue->items.size()) : 0;
}
