#pragma once

#include "application/command_sink.h"
#include "core/task_event_source.h"
#include "infrastructure/logger_service.h"
#include "infrastructure/track_node_control_protocol.h"
#include "misc/serial_read_adapter.h"

#include <atomic>
#include <span>

class console_service
{
public:
	console_service(serial_device &input, command_sink &commands,
	                logger_service &logger, supervisor &monitor,
	                std::span<task_event_source *const> wake_sources = {})
	    : input_(input), commands_(commands),
	      logger_(logger), monitor_(monitor), wake_sources_(wake_sources) {}

public: // Common lifecycle
	status start();
	void stop();

public: // Console API
	[[nodiscard]] std::uint32_t rejected_commands() const { return rejected_.load(); }

private: // Common task loop
	static void task_entry(void *argument);
	void run();
	void handle_event(protocol_event event);
	void listen_to_wake_sources();
	void wait_for_input(TickType_t poll_interval);

private: // State
	serial_read_adapter input_;
	track_node_control_protocol protocol_;
	command_sink &commands_;
	logger_service &logger_;
	supervisor &monitor_;
	std::span<task_event_source *const> wake_sources_;
	TaskHandle_t task_ = nullptr;
	std::atomic<std::uint32_t> rejected_{ 0 };
};
