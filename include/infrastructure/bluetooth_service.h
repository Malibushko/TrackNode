#pragma once

#include "application/telemetry_sink.h"
#include "application/command_sink.h"
#include "application/firmware_log_sink.h"
#include "infrastructure/bluetooth_config.h"
#include "infrastructure/diagnostic_log.h"
#include <atomic>
#include "drivers/bluetooth_backend.h"
#include "libs/freertos/freertos_helpers.h"
#include <string_view>
#include "infrastructure/command_parser.h"

class logger_service;
class supervisor;

class bluetooth_service final : public telemetry_sink, public command_reply_sink, public firmware_log_sink
{
public:
	bluetooth_service(bluetooth_backend &backend, command_sink &commands,
	                  logger_service &logger, supervisor &monitor, bluetooth_config config = {})
		: backend_(backend), commands_(commands), logger_(logger), monitor_(monitor), config_(config) {}

public:
	status start();
	void stop();

public:
	status submit(const telemetry_sample &sample) override;
	status submit_reply(const command_reply &reply) override;
	void submit_log(std::string_view text) override;
	void set_power_mode(power_mode mode) override;

private:
	status validate_config() const;
	static void task_entry(void *argument);
	void run();
	bool initialize_backend();
	status poll_and_process(bool &poll_failed);
	bool handle_failure(error failure, bool poll_failed);
	status process_advertising();
	status handle_link_changed(const bluetooth_link &link);
	status handle_received(const bluetooth_event &event);
	status handle_command(std::string_view text, std::uint32_t session);
	status handle_log_stream(bool enabled, std::uint32_t session);
	status send_parse_error(command_parse_error code, std::uint32_t session);
	status send_reply(const command_reply &reply);
	status process_replies();
	status process_sample();
	status process_logs();
	result<bool> process_diagnostic(std::uint32_t session);
	status send_log_line(std::uint32_t timestamp_ms, const char *text, std::uint32_t session);
	void reset_log_stream();
	void reset_exchange();
	status handle_event(const bluetooth_event &event);
	status send_text(std::string_view text, std::uint32_t session);
	status send_error(const char *text, std::uint32_t session);
	status process_output();

	bluetooth_backend &backend_;
	command_sink &commands_;
	logger_service &logger_;
	supervisor &monitor_;
	const bluetooth_config config_;
	QueueHandle_t replies_ = nullptr;
	QueueHandle_t samples_ = nullptr;
	QueueHandle_t logs_ = nullptr;
	TaskHandle_t task_ = nullptr;

	bluetooth_link link_{};
	std::optional<telemetry_sample> latest_sample_;
	bool measurement_pending_ = false;
	std::uint32_t request_id_ = 0;
	bool advertise_pending_ = false;
	struct log_message
	{
		std::uint32_t session;
		std::uint32_t timestamp_ms;
		std::array<char, bluetooth_config::log_text_storage_capacity> text;
	};
	std::atomic<std::uint32_t> log_session_{0};
	std::atomic<std::uint32_t> dropped_logs_{0};
	diagnostic_log diagnostics_;
};
