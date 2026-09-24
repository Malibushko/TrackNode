#pragma once

#include "application/command_sink.h"
#include "infrastructure/logger_service.h"
#include "infrastructure/track_node_control_protocol.h"
#include "misc/uart_read_adapter.h"

#include <atomic>

// UART framing and command delivery; command semantics live in node_controller.
class console_service
{
public:
	console_service(uart &input, command_sink &commands,
	                logger_service &logger, supervisor &monitor)
	    : input_(input), protocol_(input_), commands_(commands),
	      logger_(logger), monitor_(monitor) {}

public: // Common lifecycle
	status start();
	void stop();

public: // Console API
	[[nodiscard]] std::uint32_t rejected_commands() const { return rejected_.load(); }

private: // Common task loop
	static void task_entry(void *argument);
	void run();

private: // State
	uart_read_adapter input_;
	track_node_control_protocol<uart_read_adapter> protocol_;
	command_sink &commands_;
	logger_service &logger_;
	supervisor &monitor_;
	TaskHandle_t task_ = nullptr;
	std::atomic<std::uint32_t> rejected_{ 0 };
};
