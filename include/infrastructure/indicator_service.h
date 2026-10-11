#pragma once

#include "infrastructure/logger_service.h"
#include "infrastructure/status_indicator.h"
#include "infrastructure/supervisor.h"

#include <atomic>

class indicator_service
{
public:
	indicator_service(logger_service &logger, supervisor &monitor);

public: // Common lifecycle
	status start();
	void stop();

public: // Indicator API
	status init_diagnostic();
	status ping();
	void set_pairing(bool active) { pairing_.store(active); }
	void update_once(TickType_t now);
	[[nodiscard]] bool diagnostic_ready() const { return ready_.load(); }

private: // Common task loop
	static void task_entry(void *argument);
	void run();

private: // State
	gpio output_;
	status_indicator indicator_;
	logger_service &logger_;
	supervisor &monitor_;
	QueueHandle_t inbox_ = nullptr;
	TaskHandle_t task_ = nullptr;
	std::atomic<bool> pairing_{false};
	std::atomic<bool> ready_{ false };
};
