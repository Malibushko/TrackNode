#pragma once

#include "application/telemetry_sink.h"
#include "application/battery_sink.h"
#include "application/command_sink.h"
#include "application/firmware_log_sink.h"
#include "infrastructure/bluetooth_config.h"
#include "infrastructure/diagnostic_log.h"
#include "infrastructure/telemetry_journal_service.h"
#include <atomic>
#include "infrastructure/indicator_service.h"
#include "drivers/bluetooth_backend.h"
#include "libs/freertos/freertos_helpers.h"
#include <string_view>
#include "infrastructure/command_parser.h"

class logger_service;
class supervisor;

class bluetooth_service final : public telemetry_sink, public command_reply_sink, public firmware_log_sink, public battery_sink
{
public:
	bluetooth_service(bluetooth_backend &backend, command_sink &commands,
	                  logger_service &logger, supervisor &monitor, bluetooth_config config = {}, indicator_service *indicator = nullptr,
	                  telemetry_journal_service *journal = nullptr)
		: backend_(backend), commands_(commands), logger_(logger), monitor_(monitor), config_(config), indicator_(indicator), journal_(journal) {}

public:
	status start();
	void stop();

	status on_pairing_mode_requested();
	status on_advertising_requested();

	[[nodiscard]] bool can_accept_session(std::uint32_t session) const override;
	status submit(const telemetry_sample &sample) override;
	status submit_battery(const bluetooth_battery::snapshot &value) override;
	status submit_reply(const command_reply &reply) override;
	void submit_log(std::string_view text) override;
	void set_power_mode(power_mode mode) override;

private:
	[[nodiscard]] status validate_config() const;
	bool initialize_backend();

	static void task_entry(void *argument);
	void run();
	status poll_and_process(bool &poll_failed);
	[[nodiscard]] bool has_pending_work() const;
	void wake_task();

	status process_pairing_request();
	status process_advertising();
	status process_output();
	status process_replies();
	status process_sample();
	status process_battery();
	status process_history();
	status process_logs();
	result<bool> process_diagnostic(std::uint32_t session);

	status handle_event(const bluetooth_event &event);
	status handle_link_changed(const bluetooth_link &link);
	status handle_pairing_changed(bool active);
	status handle_received(const bluetooth_event &event);
	status handle_command(std::string_view text, std::uint32_t session);
	status handle_log_stream(bool enabled, std::uint32_t session);
	bool handle_failure(error failure, bool poll_failed);

	status send_reply(const command_reply &reply);
	status send_text(std::string_view text, std::uint32_t session);
	status send_error(const char *text, std::uint32_t session);
	status send_parse_error(command_parse_error code, std::uint32_t session);
	status send_log_line(std::uint32_t timestamp_ms, const char *text, std::uint32_t session);

	void reset_exchange();
	void reset_session();
	void reset_log_stream();

private:
	bluetooth_backend &backend_;
	command_sink &commands_;
	logger_service &logger_;
	supervisor &monitor_;
	const bluetooth_config config_;
	indicator_service *indicator_;
	telemetry_journal_service *journal_;

	TaskHandle_t task_ = nullptr;
	QueueHandle_t replies_ = nullptr;
	QueueHandle_t samples_ = nullptr;
	QueueHandle_t batteries_ = nullptr;
	QueueHandle_t logs_ = nullptr;

	std::atomic<bool> pairing_window_requested_{false};
	std::atomic<bool> advertising_requested_{false};
	std::atomic<std::uint32_t> command_session_{0};
	std::uint32_t blocked_session_ = 0;
	bluetooth_link link_{};
	bool advertise_pending_ = false;

	std::optional<telemetry_sample> latest_sample_;
	bool measurement_pending_ = false;
	struct queued_battery
	{
		bluetooth_battery::snapshot value;
		TickType_t received_at;
	};
	std::optional<queued_battery> latest_battery_;
	bool battery_pending_ = false;
	std::uint32_t request_id_ = 0;

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
