#include "infrastructure/bluetooth_service.h"
#include "infrastructure/command_parser.h"
#include "infrastructure/logger_service.h"
#include "infrastructure/supervisor.h"
#include "application/log_record.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<telemetry_sample>);

status bluetooth_service::validate_config() const
{
	if (config_.poll_timeout_ms == 0 || pdMS_TO_TICKS(config_.reconnect_interval_ms) == 0 ||
		config_.command_capacity == 0 || config_.command_capacity > bluetooth_event{}.payload.size() ||
		config_.reply_queue_capacity == 0 || config_.log_queue_capacity == 0 ||
		config_.reply_queue_capacity > UINT32_MAX || config_.log_queue_capacity > UINT32_MAX ||
		config_.replies_per_cycle == 0 || config_.logs_per_cycle == 0 ||
		config_.log_text_capacity == 0 || config_.log_text_capacity >= bluetooth_config::log_text_storage_capacity ||
		config_.task_stack_depth == 0 || config_.task_priority >= configMAX_PRIORITIES)
	{
		return fail(component::service, error_code::invalid_config);
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::start()
{
	if (task_ || replies_ || samples_ || logs_)
	{
		return fail(component::rtos, error_code::already_initialized);
	}
	if (auto validated = validate_config(); !validated)
	{
		return validated;
	}

	replies_ = xQueueCreate(config_.reply_queue_capacity, sizeof(command_reply));
	samples_ = xQueueCreate(1, sizeof(telemetry_sample));
	logs_ = xQueueCreate(config_.log_queue_capacity, sizeof(log_message));
	if (!replies_ || !samples_ || !logs_)
	{
		stop();
		return fail(component::rtos, error_code::out_of_memory);
	}

	const auto created = task_create(task_entry, "bluetooth", config_.task_stack_depth, this, config_.task_priority);
	if (!created)
	{
		stop();
		return tl::unexpected(created.error());
	}

	task_ = *created;
	logger_.set_log_sink(this);
	monitor_.set_health(service_id::bluetooth, service_health::starting);
	return {};
}

//////////////////////////////////////////////////////////////////////////

void bluetooth_service::stop()
{
	logger_.set_log_sink(nullptr);
	log_session_.store(0);
	if (task_)
	{
		vTaskDelete(task_);
		task_ = nullptr;
	}
	if (replies_)
	{
		vQueueDelete(replies_);
		replies_ = nullptr;
	}
	if (samples_)
	{
		vQueueDelete(samples_);
		samples_ = nullptr;
	}
	if (logs_)
	{
		vQueueDelete(logs_);
		logs_ = nullptr;
	}

	backend_.stop();
	link_ = {};
	advertise_pending_ = false;
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::submit(const telemetry_sample &sample)
{
	if (!samples_)
	{
		return fail(component::service, error_code::not_initialized);
	}
	if (!sample.sensor.valid)
	{
		return fail(component::service, error_code::invalid_argument);
	}

	return from_freertos(xQueueOverwrite(samples_, &sample));
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::submit_reply(const command_reply &reply)
{
	if (!replies_)
	{
		return fail(component::service, error_code::not_initialized);
	}
	if (xQueueSend(replies_, &reply, 0) != pdPASS)
	{
		return fail(component::service, error_code::queue_full);
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

void bluetooth_service::set_power_mode(power_mode)
{
}

//////////////////////////////////////////////////////////////////////////

void bluetooth_service::submit_log(std::string_view text)
{
	const auto session = log_session_.load();
	if (session == 0 || !logs_)
	{
		return;
	}

	log_message message{
		.session = session,
		.timestamp_ms = pdTICKS_TO_MS(xTaskGetTickCount()),
		.text = {}
	};
	std::size_t index = 0;
	for (const char value : text)
	{
		if (index == config_.log_text_capacity)
		{
			break;
		}
		message.text[index++] = value == '\r' || value == '\n' ? ' ' : value;
	}
	if (xQueueSend(logs_, &message, 0) != pdPASS)
	{
		dropped_logs_.fetch_add(1);
	}
}

//////////////////////////////////////////////////////////////////////////

void bluetooth_service::reset_log_stream()
{
	log_session_.store(0);
	dropped_logs_.store(0);
}

//////////////////////////////////////////////////////////////////////////

void bluetooth_service::reset_exchange()
{
	reset_log_stream();
	xQueueReset(replies_);
	xQueueReset(samples_);
	xQueueReset(logs_);
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::send_text(std::string_view text, std::uint32_t session)
{
	if (!link_.connected || !link_.subscribed || link_.session != session || link_.max_payload == 0)
	{
		return fail(component::service, error_code::not_ready);
	}

	while (!text.empty())
	{
		const auto size = std::min<std::size_t>(text.size(), link_.max_payload);
		const auto sent = backend_.send({reinterpret_cast<const std::uint8_t *>(text.data()), size}, session);
		if (!sent)
		{
			return sent;
		}

		text.remove_prefix(size);
		monitor_.progress(service_id::bluetooth, xTaskGetTickCount());
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::send_error(const char *text, std::uint32_t session)
{
	command_reply reply{
		.request_id = request_id_,
		.session = session
	};
	const auto size = std::strlen(text);
	if (size >= reply.text.size())
	{
		return fail(component::service, error_code::invalid_argument);
	}

	std::copy_n(text, size, reply.text.begin());
	return submit_reply(reply);
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::handle_event(const bluetooth_event &event)
{
	switch (event.type)
	{
		case bluetooth_event::kind::link_changed:
			return handle_link_changed(event.link);
		case bluetooth_event::kind::received:
			return handle_received(event);
		default:
			return {};
	}
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::handle_link_changed(const bluetooth_link &link)
{
	if (link.session != link_.session || !link.connected || !link.subscribed)
	{
		reset_exchange();
	}
	link_ = link;
	advertise_pending_ = !link_.connected;
	return {};
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::handle_received(const bluetooth_event &event)
{
	if (!link_.connected || event.link.session != link_.session)
	{
		return {};
	}

	if (++request_id_ == 0)
	{
		++request_id_;
	}
	if (event.size > event.payload.size())
	{
		return fail(component::service, error_code::invalid_argument);
	}

	std::string_view text{reinterpret_cast<const char *>(event.payload.data()), event.size};
	while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
	{
		text.remove_suffix(1);
	}
	if (text.size() > config_.command_capacity)
	{
		return send_error("ERR LINE_TOO_LONG\r\n", event.link.session);
	}
	if (text.empty() || text.find_first_of(std::string_view{"\0\r\n", 3}) != std::string_view::npos)
	{
		return send_error("ERR INVALID_SYNTAX\r\n", event.link.session);
	}

	return handle_command(text, event.link.session);
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::handle_command(std::string_view text, std::uint32_t session)
{
	if (text == "SET LOG_STREAM ON" || text == "SET LOG_STREAM OFF")
	{
		return handle_log_stream(text == "SET LOG_STREAM ON", session);
	}
	const auto parsed = parse_command(text);
	if (!parsed)
	{
		return send_parse_error(parsed.error(), session);
	}

	const auto accepted = commands_.submit(command_request{
		.value = *parsed,
		.reply_to = this,
		.request_id = request_id_,
		.session = session
	});
	if (!accepted)
	{
		return send_error("ERR COMMAND_QUEUE_FULL\r\n", session);
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::handle_log_stream(bool enabled, std::uint32_t session)
{
	reset_log_stream();
	log_message discarded{};
	while (xQueueReceive(logs_, &discarded, 0) == pdPASS)
	{
	}
	if (enabled && link_.subscribed)
	{
		log_session_.store(session);
		(void)logger_.write("BLE firmware log stream enabled\r\n");
	}
	return send_error(enabled ? "OK LOG_STREAM ON\r\n" : "OK LOG_STREAM OFF\r\n", session);
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::send_parse_error(command_parse_error code, std::uint32_t session)
{
	switch (code)
	{
		case command_parse_error::unknown_command:
			return send_error("ERR UNKNOWN_COMMAND\r\n", session);
		case command_parse_error::invalid_number:
			return send_error("ERR INVALID_NUMBER\r\n", session);
		default:
			return send_error("ERR INVALID_SYNTAX\r\n", session);
	}
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::process_output()
{
	return process_replies()
		.and_then([&]{ return process_sample(); })
		.and_then([&]{ return process_logs(); });
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::send_reply(const command_reply &reply)
{
	if (!link_.connected || !link_.subscribed || reply.session != link_.session)
	{
		return {};
	}

	const auto end = std::find(reply.text.begin(), reply.text.end(), '\0');
	if (end == reply.text.end())
	{
		return fail(component::service, error_code::invalid_argument);
	}

	return send_text({reply.text.data(), static_cast<std::size_t>(end - reply.text.begin())}, reply.session);
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::process_replies()
{
	command_reply reply{};
	for (std::size_t index = 0; index < config_.replies_per_cycle && xQueueReceive(replies_, &reply, 0) == pdPASS; ++index)
	{
		if (const auto sent = send_reply(reply); !sent)
		{
			return sent;
		}
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::process_sample()
{
	telemetry_sample sample{};
	if (xQueueReceive(samples_, &sample, 0) != pdPASS || !link_.connected || !link_.subscribed)
	{
		return {};
	}

	const auto record = format_sample_csv(sample.sensor, sample.gnss, sample.recorded_at);
	if (!record)
	{
		return tl::unexpected(record.error());
	}

	std::array<char, log_record{}.text.size() + 2> text{};
	text[0] = 'T';
	text[1] = ',';
	std::copy_n(record->text.begin(), record->length, text.begin() + 2);
	return send_text({text.data(), static_cast<std::size_t>(record->length) + 2}, link_.session);
}

//////////////////////////////////////////////////////////////////////////

void bluetooth_service::task_entry(void *argument)
{
	static_cast<bluetooth_service *>(argument)->run();
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::send_log_line(std::uint32_t timestamp_ms, const char *text, std::uint32_t session)
{
	std::array<char, bluetooth_config::log_line_storage_capacity> line{};
	const int length = std::snprintf(line.data(), line.size(), "L,%lu,%s\r\n",
		static_cast<unsigned long>(timestamp_ms), text);
	if (length < 0 || static_cast<std::size_t>(length) >= line.size())
	{
		return fail(component::service, error_code::invalid_argument);
	}
	return send_text({line.data(), static_cast<std::size_t>(length)}, session);
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::process_logs()
{
	const auto session = log_session_.load();
	log_message message{};
	if (session == 0)
	{
		while (xQueueReceive(logs_, &message, 0) == pdPASS)
		{
		}
		return {};
	}

	const auto dropped = dropped_logs_.exchange(0);
	if (dropped != 0)
	{
		std::array<char, bluetooth_config::log_line_storage_capacity> text{};
		std::snprintf(text.data(), text.size(), "[dropped %lu BLE log messages]", static_cast<unsigned long>(dropped));
		if (auto sent = send_log_line(pdTICKS_TO_MS(xTaskGetTickCount()), text.data(), session); !sent)
		{
			dropped_logs_.fetch_add(dropped);
			return sent;
		}
	}
	for (std::size_t index = 0; index < config_.logs_per_cycle && xQueueReceive(logs_, &message, 0) == pdPASS; ++index)
	{
		if (message.session != session)
		{
			continue;
		}
		if (auto sent = send_log_line(message.timestamp_ms, message.text.data(), session); !sent)
		{
			dropped_logs_.fetch_add(1);
			return sent;
		}
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

bool bluetooth_service::initialize_backend()
{
	reset_exchange();
	link_ = {};
	advertise_pending_ = false;
	const auto initialized = backend_.init();
	if (!initialized)
	{
		monitor_.report(service_id::bluetooth, initialized.error(), service_health::degraded);
		(void)logger_.write("BLE bridge init failed: %u\r\n", static_cast<unsigned>(initialized.error().code));
		monitor_.progress(service_id::bluetooth, xTaskGetTickCount());
		vTaskDelay(pdMS_TO_TICKS(config_.reconnect_interval_ms));
		return false;
	}

	monitor_.set_health(service_id::bluetooth, service_health::ready);
	return true;
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::process_advertising()
{
	if (!advertise_pending_)
	{
		return {};
	}

	const auto outcome = backend_.start_advertise();
	advertise_pending_ = !outcome.has_value();
	return outcome;
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::poll_and_process(bool &poll_failed)
{
	const auto event = backend_.poll(config_.poll_timeout_ms);
	poll_failed = !event.has_value();
	if (!event)
	{
		return tl::unexpected(event.error());
	}

	return handle_event(*event)
		.and_then([&]{ return process_advertising(); })
		.and_then([&]{ return process_output(); });
}

//////////////////////////////////////////////////////////////////////////

bool bluetooth_service::handle_failure(error failure, bool poll_failed)
{
	monitor_.report(service_id::bluetooth, failure, service_health::degraded);
	(void)logger_.write("BLE bridge failed: %u\r\n", static_cast<unsigned>(failure.code));
	if (poll_failed || (failure.code != error_code::not_ready && failure.code != error_code::queue_full))
	{
		reset_exchange();
		link_ = {};
		backend_.stop();
		return false;
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////

void bluetooth_service::run()
{
	bool ready = false;
	for (;;)
	{
		if (!ready)
		{
			ready = initialize_backend();
			if (!ready)
			{
				continue;
			}
		}

		bool poll_failed = false;
		const auto outcome = poll_and_process(poll_failed);
		if (!outcome)
		{
			ready = handle_failure(outcome.error(), poll_failed);
		}
		else
		{
			monitor_.set_health(service_id::bluetooth, service_health::ready);
		}

		monitor_.progress(service_id::bluetooth, xTaskGetTickCount());
	}
}
