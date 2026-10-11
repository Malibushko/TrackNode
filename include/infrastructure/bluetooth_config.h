#pragma once

#include "application/command_reply.h"
#include "libs/freertos/freertos_helpers.h"
#include <cstddef>
#include <cstdint>

struct bluetooth_config
{
	static constexpr std::size_t log_text_storage_capacity = command_reply{}.text.size();
	static constexpr std::size_t log_line_storage_capacity = log_text_storage_capacity + sizeof("L,4294967295,\r\n") - 1;

	std::uint32_t poll_timeout_ms = 20;
	std::uint32_t reconnect_interval_ms = 1000;
	std::size_t command_capacity = 64;
	std::size_t reply_queue_capacity = 4;
	std::size_t log_queue_capacity = 8;
	std::size_t replies_per_cycle = 4;
	std::size_t logs_per_cycle = 2;
	std::size_t log_text_capacity = log_text_storage_capacity - 1;
	configSTACK_DEPTH_TYPE task_stack_depth = configMINIMAL_STACK_SIZE * 8;
	UBaseType_t task_priority = tskIDLE_PRIORITY + 1;
};
