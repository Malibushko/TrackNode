#pragma once

#include "application/telemetry_sink.h"
#include "application/telemetry_archive_policy.h"
#include "infrastructure/telemetry_history_protocol.h"
#include "infrastructure/telemetry_flash_journal.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <optional>

class telemetry_journal_service final : public telemetry_sink
{
public:
	explicit telemetry_journal_service(telemetry_sink &downstream, w25qxx &flash, telemetry_archive_config config = {},
		std::array<std::uint32_t, 3> device_id = {})
		: downstream_(downstream), flash_(flash), policy_(config), device_id_(device_id) {}

	status init();
	status submit(const telemetry_sample &sample) override;
	status acknowledge(sequence_t sample_id) override;
	[[nodiscard]] sequence_t next_sample_id() const override { return journal_.next_sample_id(); }

	void set_power_mode(power_mode mode) override;

	[[nodiscard]] bool storage_available() const { return storage_available_; }
	[[nodiscard]] std::optional<error> storage_failure() const { return storage_failure_; }

	[[nodiscard]] bool can_handle_command(std::string_view text) const;
	std::string_view handle_history_command(std::string_view text, std::uint32_t session);
	std::string_view next_history_response();
	[[nodiscard]] bool history_response_pending() const { return sending_page_; }
	void on_history_response_sent();
	void reset_history_transfer();

private:
	std::uint64_t update_uptime();
	[[nodiscard]] std::uint32_t sample_seconds(TickType_t sample_tick, std::uint64_t uptime) const;
	[[nodiscard]] telemetry_record pack_sample(const telemetry_sample &sample, std::uint64_t uptime) const;
	status archive(const telemetry_sample &sample);

	[[nodiscard]] bool is_representable_environment(const sensor_sample &environment) const;

	std::string_view handle_open_snapshot(sequence_t nonce, std::uint32_t session);
	std::string_view handle_read_page(sequence_t after_sample_id);
	std::string_view handle_history_ack(sequence_t sample_id);

private:
	telemetry_sink &downstream_;
	w25qxx &flash_;
	telemetry_flash_journal journal_;
	telemetry_archive_policy policy_;
	const std::array<std::uint32_t, 3> device_id_;
	SemaphoreHandle_t lock_ = nullptr;
	bool storage_available_ = false;
	std::optional<error> storage_failure_;

	TickType_t previous_tick_ = 0;
	std::uint64_t elapsed_ticks_ = 0;
	sequence_t last_received_sample_id_ = 0;
	sequence_t confirmed_through_sample_id_ = 0;
	std::uint64_t boot_epoch_ = 0;

	std::uint32_t sync_session_ = 0;
	sequence_t snapshot_last_sample_id_ = 0;
	sequence_t read_after_sample_id_ = 0;
	std::uint64_t snapshot_overwritten_count_ = 0;
	telemetry_journal_page page_{};
	std::size_t response_index_ = 0;
	bool sending_page_ = false;
	bool page_transmitted_ = false;
	telemetry_history_protocol::response_buffer response_{};
};
