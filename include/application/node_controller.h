#pragma once

#include "application/command_sink.h"
#include "application/sample.h"

#include <optional>

class sensor_service;
class storage_service;
class indicator_service;
class logger_service;
class supervisor;
class gnss_service;
class radio_service;

// The application workflow: sampling cadence, command meaning and data flow.
class node_controller final : public command_sink
{
public:
	node_controller(sensor_service &sensor,
#ifndef NO_STORAGE
	                storage_service &storage,
#endif
	                indicator_service &indicator, logger_service &logger,
	                gnss_service &gnss, radio_service &radio, supervisor &monitor);

public: // Common lifecycle
	status start();
	void stop();

public: // Application API
	status submit(command value) override;

private: // Common task loop
	static void task_entry(void *argument);
	void run();
	status init_sensor();

private: // Application behavior
	void take_sample();
	void handle_command(command value);
	void handle_ping();
	void handle_read_temperature();
	void handle_read_pressure();
	void handle_read_gnss();
	void handle_read_log();
	void handle_set_sample_period(std::uint32_t period_ms);
	void handle_reset_storage();
	bool ensure_sample_available();

private: // State
	sensor_service &sensor_;
#ifndef NO_STORAGE
	storage_service &storage_;
#endif
	indicator_service &indicator_;
	logger_service &logger_;
	gnss_service &gnss_;
	radio_service &radio_;
	supervisor &monitor_;
	QueueHandle_t inbox_ = nullptr;
	TaskHandle_t task_ = nullptr;
	std::optional<sensor_sample> last_sample_;
	std::uint32_t next_sequence_ = 1;
	TickType_t sample_period_ = pdMS_TO_TICKS(3000);
	bool sensor_initialized_ = false;
	bool sensor_faulted_ = false;
};
