#pragma once

#include "application/radio_packet.h"
#include "common/power_mode.h"
#include "drivers/devices/sx126x.h"
#include "infrastructure/logger_service.h"
#include "infrastructure/supervisor.h"

#include <atomic>

class radio_service
{
public:
	radio_service(sx126x_driver &radio, logger_service &logger, supervisor &monitor)
		: radio_(radio), logger_(logger), monitor_(monitor) {}

	status start();
	void set_power_mode(power_mode mode) { power_mode_.store(mode, std::memory_order_relaxed); }
	void stop();
	status submit(const sensor_sample &sample, const std::optional<gnss_sample> &gnss,
	              TickType_t recorded_at);
	[[nodiscard]] std::uint32_t dropped_count() const { return dropped_.load(std::memory_order_relaxed); }

private:
	struct radio_job
	{
		sensor_sample sample;
		std::optional<gnss_sample> gnss;
		TickType_t recorded_at;
	};

	static void task_entry(void *argument);
	void run();

	sx126x_driver &radio_;
	logger_service &logger_;
	supervisor &monitor_;
	QueueHandle_t inbox_ = nullptr;
	TaskHandle_t task_ = nullptr;
	std::atomic<std::uint32_t> dropped_{0};
	std::atomic<power_mode> power_mode_{power_mode::high};
};
