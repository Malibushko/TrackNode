#include "infrastructure/bluetooth_service.h"
#include "infrastructure/logger_service.h"
#include "infrastructure/supervisor.h"
#include "infrastructure/bluetooth_telemetry.h"
#include "application/telemetry_router.h"
#include <deque>
#include <functional>
#include <iostream>
#include <stdexcept>

void require(bool condition)
{
	if (!condition)
	{
		throw std::runtime_error("Bluetooth service test failed");
	}
}

struct finished {};
class fake_backend final : public bluetooth_backend
{
public:
	std::deque<bluetooth_event> events;
	std::vector<std::string> chunks;
	std::vector<std::uint32_t> sessions;
	std::vector<std::vector<std::uint8_t>> measurements;
	std::vector<std::vector<std::uint8_t>> batteries;
	status publish_battery(std::span<const std::uint8_t> data) override
	{
		batteries.emplace_back(data.begin(), data.end());
		return {};
	}
	status publish_measurement(std::span<const std::uint8_t> data) override
	{
		measurements.emplace_back(data.begin(), data.end());
		return {};
	}
	std::function<void()> before_finish;
	std::function<void(const bluetooth_event &)> before_event;
	unsigned initialized = 0;
	unsigned advertisements = 0;
	std::uint32_t last_poll_timeout_ms = 0;
	std::vector<std::uint32_t> poll_timeouts;
	bool fail_init = false;
	bool busy_advertise = false;
	bool restart_poll = false;
	unsigned pairings = 0;

	status init() override
	{
		++initialized;
		if (fail_init)
		{
			fail_init = false;
			return fail(component::service, error_code::timeout_error);
		}

		return {};
	}
	void stop() override {}
	status start_advertise() override
	{
		++advertisements;
		if (busy_advertise)
		{
			busy_advertise = false;
			return fail(component::service, error_code::queue_full);
		}

		return {};
	}
	status open_pairing_window() override { ++pairings; return {}; }
	status stop_advertise() override
	{
		return {};
	}
	status send(std::span<const std::uint8_t> data, std::uint32_t session) override
	{
		chunks.emplace_back(reinterpret_cast<const char *>(data.data()), data.size());
		sessions.push_back(session);
		return {};
	}
	result<bluetooth_event> poll(std::uint32_t timeout_ms) override
	{
		last_poll_timeout_ms = timeout_ms;
		poll_timeouts.push_back(timeout_ms);
		if (restart_poll)
		{
			restart_poll = false;
			return fail(component::service, error_code::not_ready);
		}
		if (events.empty())
		{
			if (before_finish)
			{
				auto callback = std::move(before_finish);
				callback();
				if (restart_poll)
				{
					restart_poll = false;
					return fail(component::service, error_code::not_ready);
				}
				return bluetooth_event{};
			}

			throw finished{};
		}

		const auto event = events.front();
		events.pop_front();
		if (before_event)
		{
			before_event(event);
		}
		return event;
	}
};

class fake_commands final : public command_sink
{
public:
	std::vector<command_request> received;
	status submit(command_request request) override
	{
		received.push_back(request);
		command_reply reply{
			.request_id = request.request_id,
			.session = request.session,
			.text = {'P', 'O', 'N', 'G', '\r', '\n'}
		};
		return request.reply_to->submit_reply(reply);
	}
};

bluetooth_event receive(std::string_view text, std::uint32_t session)
{
	bluetooth_event event{
		.type = bluetooth_event::kind::received,
		.link = {true, true, 3, session},
		.size = text.size()
	};
	std::copy(text.begin(), text.end(), event.payload.begin());
	return event;
}

int main()
{
	fake_backend backend;
	fake_commands commands;
	logger_service logger;
	supervisor monitor;
	bluetooth_service service{backend, commands, logger, monitor};
	require(!service.submit_reply({}));
	require(service.start().has_value());
	require(backend.initialized == 0);
	require(!service.start());

	backend.fail_init = true;
	backend.busy_advertise = true;
	backend.events.push_back(bluetooth_event{
		.type = bluetooth_event::kind::link_changed
	});
	backend.events.push_back({});
	backend.events.push_back(bluetooth_event{
		.type = bluetooth_event::kind::link_changed,
		.link = {true, true, 3, 10}
	});
	backend.events.push_back(receive("PING\r\n", 10));
	backend.events.push_back(receive("BAD", 10));
	backend.events.push_back(receive("PING\nPING", 10));
	backend.events.push_back(receive("PING", 9));
	backend.events.push_back(receive("BOOTLOAD", 10));
	backend.events.push_back(receive("GET BATTERY", 10));
	backend.events.push_back(bluetooth_event{
		.type = bluetooth_event::kind::link_changed,
		.link = {true, true, 20, 11}
	});
	backend.before_finish = [&]
	{
		command_reply stale{
			.session = 10,
			.text = {'S', 'T', 'A', 'L', 'E'}
		};
		require(service.submit_reply(stale).has_value());
		telemetry_sample sample{
			.sensor = {
				.sequence = 1,
				.temperature_c = 20,
				.pressure_pa = 100000,
				.valid = true
			}
		};
		require(service.submit(sample).has_value());
		sample.sensor.temperature_c = 25;
		require(service.submit(sample).has_value());
	};
	try
	{
		fake_task(fake_argument);
	}
	catch (const finished &)
	{
	}

	require(commands.received.size() == 3);
	require(commands.received[0].value.type == command_type::ping);
	require(commands.received[0].session == 10 && commands.received[0].reply_to == &service);
	require(commands.received[1].value.type == command_type::bootload);
	require(commands.received[1].session == 10 && commands.received[1].reply_to == &service);
	require(commands.received[2].value.type == command_type::read_battery);
	require(commands.received[2].session == 10 && commands.received[2].reply_to == &service);
	require(backend.initialized == 2 && backend.advertisements == 2);
	std::string text;
	for (std::size_t index = 0; index < backend.chunks.size(); ++index)
	{
		text += backend.chunks[index];
		require(backend.chunks[index].size() <= (backend.sessions[index] == 10 ? 3u : 20u));
	}
	require(text.starts_with("PONG\r\nERR UNKNOWN_COMMAND\r\nERR INVALID_SYNTAX\r\n"));
	require(text.find("STALE") == std::string::npos);
	require(text.find("T,0,25.00,100000.00") != std::string::npos);
	require(monitor.errors == 2);
	service.stop();
	require(logger.sink == nullptr);

	fake_backend cache_backend;
	fake_commands cache_commands;
	bluetooth_service cache_service{cache_backend, cache_commands, logger, monitor};
	require(cache_service.start().has_value());
	telemetry_sample cached{
		.sensor = {
			.sequence = 42,
			.temperature_c = -1.25f,
			.pressure_pa = 100000,
			.valid = true
		}
	};
	require(cache_service.submit(cached).has_value());
	bluetooth_battery::snapshot battery{
		.flags = bluetooth_battery::reading_valid,
		.level = 72,
		.battery_mv = 3981,
		.supply_mv = 3300,
		.battery_raw = 2470,
		.vrefint_raw = 1500,
		.minimum_raw = 2460,
		.maximum_raw = 2480
	};
	require(cache_service.submit_battery(battery).has_value());
	cache_backend.events.push_back(bluetooth_event{});
	cache_backend.events.push_back(bluetooth_event{
		.type = bluetooth_event::kind::link_changed,
		.link = {true, false, 20, 301}
	});
	cache_backend.events.push_back(bluetooth_event{
		.type = bluetooth_event::kind::link_changed,
		.link = {false, false, 20, 301}
	});
	cache_backend.events.push_back(bluetooth_event{
		.type = bluetooth_event::kind::link_changed,
		.link = {true, false, 20, 302}
	});
	cache_backend.before_finish = [&]
	{
		cache_backend.restart_poll = true;
		cache_backend.events.push_back(bluetooth_event{});
	};
	try
	{
		fake_task(fake_argument);
	}
	catch (const finished &)
	{
	}
	require(cache_backend.measurements.size() == 4 && cache_backend.chunks.empty());
	require(cache_backend.batteries.size() == 4);
	for (const auto &bytes : cache_backend.batteries)
	{
		const auto value = bluetooth_battery::decode(bytes);
		require(value && value->battery_mv == 3981 && value->level == 72);
	}
	require(cache_backend.initialized == 2);
	for (const auto &bytes : cache_backend.measurements)
	{
		const auto value = bluetooth_measurement::decode(bytes);
		require(value && value->sensor_sequence == 42 && value->temperature == -125 && value->pressure == 1000000);
	}
	cached.sensor.temperature_c = std::numeric_limits<float>::quiet_NaN();
	require(!(format_bluetooth_measurement(cached, 100, 600000).flags & bluetooth_measurement::sensor_valid));
	cached.sensor.temperature_c = 400;
	require(!(format_bluetooth_measurement(cached, 100, 600000).flags & bluetooth_measurement::sensor_valid));
	cache_service.stop();

	fake_backend log_backend;
	fake_commands log_commands;
	bluetooth_service log_service{log_backend, log_commands, logger, monitor};
	require(log_service.start().has_value());
	require(logger.sink == &log_service);
	log_backend.events.push_back(bluetooth_event{
		.type = bluetooth_event::kind::link_changed,
		.link = {true, true, 20, 42}
	});
	log_backend.events.push_back(receive("SET LOG_STREAM ON", 42));
	log_backend.before_finish = [&]
	{
		for (unsigned index = 0; index < 10; ++index)
		{
			logger.sink->submit_log("test,line\r\n");
		}
		require(log_service.submit_reply(command_reply{
			.session = 42,
			.text = {'P', 'R', 'I', 'O', 'R', 'I', 'T', 'Y', '\r', '\n'}
		}).has_value());
		log_backend.events.push_back(bluetooth_event{
			.type = bluetooth_event::kind::link_changed,
			.link = {true, true, 20, 43}
		});
		log_backend.events.push_back(receive("SET LOG_STREAM OFF", 43));
	};
	try
	{
		fake_task(fake_argument);
	}
	catch (const finished &)
	{
	}
	text.clear();
	for (const auto &chunk : log_backend.chunks)
	{
		require(chunk.size() <= 20);
		text += chunk;
	}
	require(text.starts_with("OK LOG_STREAM ON\r\nPRIORITY\r\nL,"));
	require(text.find("[dropped 2 BLE log messages]") != std::string::npos);
	require(text.find("test,line  \r\n") != std::string::npos);
	require(text.find("OK LOG_STREAM OFF\r\n") != std::string::npos);
	require(log_commands.received.empty());
	const auto first_log = text.find("test,line");
	const auto second_log = text.find("test,line", first_log + 1);
	require(second_log != std::string::npos && text.find("test,line", second_log + 1) == std::string::npos);
	log_service.stop();
	require(logger.sink == nullptr);

	const bluetooth_config custom_config{
		.poll_timeout_ms = 7,
		.reconnect_interval_ms = 37,
		.command_capacity = 20,
		.reply_queue_capacity = 2,
		.log_queue_capacity = 3,
		.replies_per_cycle = 1,
		.logs_per_cycle = 1,
		.log_text_capacity = 4,
		.task_stack_depth = 512,
		.task_priority = 2
	};
	fake_backend custom_backend;
	fake_commands custom_commands;
	bluetooth_service custom_service{custom_backend, custom_commands, logger, monitor, custom_config};
	require(custom_service.start().has_value());
	require(fake_stack_depth == 512 && fake_priority == 2);
	const auto initial_ticks = fake_ticks;
	custom_backend.fail_init = true;
	custom_backend.events.push_back(bluetooth_event{
		.type = bluetooth_event::kind::link_changed,
		.link = {true, true, 20, 50}
	});
	custom_backend.events.push_back(receive("SET SAMPLE_PERIOD 100", 50));
	custom_backend.events.push_back(receive("SET LOG_STREAM ON", 50));
	custom_backend.before_finish = [&]
	{
		for (unsigned index = 0; index < 5; ++index)
		{
			custom_service.submit_log("abcdef");
		}
		command_reply reply{
			.session = 50,
			.text = {'R', '\r', '\n'}
		};
		require(custom_service.submit_reply(reply).has_value());
		require(custom_service.submit_reply(reply).has_value());
		require(!custom_service.submit_reply(reply));
		custom_backend.events.push_back(bluetooth_event{
			.type = bluetooth_event::kind::link_changed,
			.link = {true, true, 20, 51}
		});
	};
	try
	{
		fake_task(fake_argument);
	}
	catch (const finished &)
	{
	}
	const auto polled_quickly_while_work_pending = std::find(custom_backend.poll_timeouts.begin(),
		custom_backend.poll_timeouts.end(), 7u) != custom_backend.poll_timeouts.end();
	const auto idle_poll_waits_for_events = custom_backend.last_poll_timeout_ms == bluetooth_config{}.idle_timeout_ms;
	require(fake_ticks - initial_ticks == 37 && polled_quickly_while_work_pending && idle_poll_waits_for_events);
	text.clear();
	for (const auto &chunk : custom_backend.chunks)
	{
		text += chunk;
	}
	require(text.starts_with("ERR LINE_TOO_LONG\r\nOK LOG_STREAM ON\r\nR\r\nL,"));
	require(text.find("[dropped 2 BLE log messages]") != std::string::npos);
	require(text.find(",abcd\r\n") != std::string::npos);
	require(text.find(",abcd\r\n", text.find(",abcd\r\n") + 1) == std::string::npos);
	require(text.find("R\r\n", text.find("R\r\n") + 1) == std::string::npos);
	require(custom_commands.received.empty());
	custom_service.stop();

	fake_backend recovery_backend;
	fake_commands recovery_commands;
	bluetooth_service recovery_service{recovery_backend, recovery_commands, logger, monitor};
	require(recovery_service.start().has_value());
	recovery_backend.events.push_back(bluetooth_event{
		.type = bluetooth_event::kind::link_changed,
		.link = {true, true, 20, 100}
	});
	recovery_backend.events.push_back(receive("SET LOG_STREAM ON", 100));
	recovery_backend.before_finish = [&]
	{
		require(recovery_service.submit_reply(command_reply{
			.session = 100,
			.text = {'O', 'L', 'D', '\r', '\n'}
		}).has_value());
		logger.sink->submit_log("OLD LOG");
		recovery_backend.restart_poll = true;
		recovery_backend.events.push_back(bluetooth_event{
			.type = bluetooth_event::kind::link_changed,
			.link = {true, true, 20, 101}
		});
		recovery_backend.events.push_back(receive("PING", 101));
	};
	recovery_backend.before_event = [&](const bluetooth_event &event)
	{
		if (event.link.session == 101 && event.type == bluetooth_event::kind::received)
		{
			require(recovery_service.submit_reply(command_reply{
				.session = 100,
				.text = {'L', 'A', 'T', 'E', '\r', '\n'}
			}).has_value());
			logger.sink->submit_log("STALE LOG AFTER REBOOT");
		}
	};
	try
	{
		fake_task(fake_argument);
	}
	catch (const finished &)
	{
	}
	text.clear();
	for (const auto &chunk : recovery_backend.chunks)
	{
		text += chunk;
	}
	require(recovery_backend.initialized == 2);
	require(text == "OK LOG_STREAM ON\r\nPONG\r\n");
	recovery_service.stop();

	fake_backend diagnostic_backend;
	fake_commands diagnostic_commands;
	supervisor diagnostic_monitor;
	diagnostic_monitor.report(service_id::console, error{component::uart, error_code::uart_error, HAL_ERROR, 8}, service_health::degraded);
	bluetooth_service diagnostic_service{diagnostic_backend, diagnostic_commands, logger, diagnostic_monitor};
	require(diagnostic_service.start().has_value());
	diagnostic_backend.events.push_back(bluetooth_event{
		.type = bluetooth_event::kind::link_changed,
		.link = {true, true, 120, 200}
	});
	diagnostic_backend.events.push_back(receive("SET LOG_STREAM ON", 200));
	diagnostic_backend.events.push_back(bluetooth_event{});
	diagnostic_backend.events.push_back(bluetooth_event{});
	diagnostic_backend.before_finish = [&]
	{
		diagnostic_monitor.set_health(service_id::console, service_health::ready);
		diagnostic_monitor.report(service_id::logger, error{component::uart, error_code::timeout_error}, service_health::degraded);
		diagnostic_backend.events.push_back(bluetooth_event{});
		diagnostic_backend.events.push_back(receive("SET LOG_STREAM OFF", 200));
		diagnostic_backend.events.push_back(receive("SET LOG_STREAM ON", 200));
	};
	try
	{
		fake_task(fake_argument);
	}
	catch (const finished &)
	{
	}
	text.clear();
	for (const auto &chunk : diagnostic_backend.chunks)
	{
		text += chunk;
	}
	const auto console_fault = text.find("HEALTH console degraded component=");
	require(console_fault != std::string::npos);
	require(text.find("detail=8\r\n", console_fault) != std::string::npos);
	require(text.find("HEALTH console degraded", console_fault + 1) == std::string::npos);
	require(text.find("HEALTH console ready\r\n") != std::string::npos);
	const auto logger_fault = text.find("HEALTH logger degraded component=");
	require(logger_fault != std::string::npos);
	require(text.find("HEALTH logger degraded component=", logger_fault + 1) != std::string::npos);
	diagnostic_service.stop();

	fake_backend pairing_backend;
	fake_commands pairing_commands;
	bluetooth_service pairing_service{pairing_backend, pairing_commands, logger, monitor};
	require(pairing_service.start().has_value());
	pairing_backend.events.push_back(bluetooth_event{
		.type = bluetooth_event::kind::link_changed,
		.link = {true, true, 20, 500}
	});
	pairing_backend.before_finish = [&]
	{
		require(pairing_service.can_accept_session(500));
		require(pairing_service.submit_reply(command_reply{.session = 500, .text = {'O', 'L', 'D'}}).has_value());
		require(pairing_service.on_pairing_mode_requested().has_value());
		require(!pairing_service.can_accept_session(500));
		pairing_backend.events.push_back(bluetooth_event{
			.type = bluetooth_event::kind::link_changed,
			.link = {true, true, 20, 500}
		});
		pairing_backend.events.push_back(receive("PING", 500));
		pairing_backend.events.push_back(bluetooth_event{
			.type = bluetooth_event::kind::link_changed,
			.link = {true, true, 20, 501}
		});
		pairing_backend.events.push_back(receive("PING", 501));
	};
	try
	{
		fake_task(fake_argument);
	}
	catch (const finished &)
	{
	}
	text.clear();
	for (const auto &chunk : pairing_backend.chunks)
	{
		text += chunk;
	}
	require(pairing_backend.pairings == 1 && text == "PONG\r\n");
	require(pairing_commands.received.size() == 1 && pairing_commands.received.front().session == 501);
	pairing_service.stop();

	fake_backend history_backend;
	fake_commands history_commands;
	telemetry_router<1> live_router;
	w25qxx history_flash;
	telemetry_journal_service journal{live_router, history_flash};
	require(journal.init().has_value());
	const auto history_base = journal.next_sample_id() - 1;
	bluetooth_service history_service{history_backend, history_commands, logger, monitor, {}, nullptr, &journal};
	require(live_router.add(history_service).has_value());
	live_router.seal();
	require(history_service.start().has_value());
	telemetry_sample archived_sample;
	archived_sample.id = history_base + 10;
	archived_sample.sensor = {fake_ticks, 1, 20, 100000, true};
	require(journal.submit(archived_sample).has_value());
	archived_sample.id = history_base + 11;
	archived_sample.sensor.temperature_c = 20.1f;
	require(journal.submit(archived_sample).has_value());
	archived_sample.id = history_base + 42;
	archived_sample.sensor.temperature_c = 21;
	require(journal.submit(archived_sample).has_value());
	history_backend.events.push_back({.type = bluetooth_event::kind::link_changed, .link = {true, true, 20, 800}});
	history_backend.events.push_back(receive("H O 000000000000002A", 800));
	history_backend.events.push_back(receive("H R 0000000000000000", 800));
	for (unsigned index = 0; index < 3; ++index)
	{
		history_backend.events.push_back({});
	}
	history_backend.events.push_back(receive("H A 000000010000002A", 800));
	history_backend.events.push_back(receive("H O 0000000000000044", 800));
	history_backend.before_event = [&](const bluetooth_event &event)
	{
		if (event.type == bluetooth_event::kind::received && event.payload[2] == 'A')
		{
			archived_sample.id = history_base + 100;
			archived_sample.sensor.temperature_c = 22;
			require(journal.submit(archived_sample).has_value());
		}
	};
	try
	{
		fake_task(fake_argument);
	}
	catch (const finished &) {}
	text.clear();
	for (const auto &chunk : history_backend.chunks)
	{
		require(chunk.size() <= 20);
		text += chunk;
	}
	require(text.find("H B 0000000000000000 000000010000002A 00000002\r\n") != std::string::npos);
	require(text.find("H D 000000010000000B") == std::string::npos);
	require(text.find("H A 000000010000002A\r\n") != std::string::npos);
	require(text.find("0000000100000064 0000000100000064 00000001") != std::string::npos);
	require(!history_backend.measurements.empty() && history_commands.received.empty());
	history_service.stop();

	fake_backend idle_backend;
	fake_commands idle_commands;
	bluetooth_service idle_service{idle_backend, idle_commands, logger, monitor};
	require(idle_service.start().has_value());
	idle_backend.events.push_back(bluetooth_event{
		.type = bluetooth_event::kind::link_changed,
		.link = {true, true, 20, 70}
	});
	std::vector<std::uint32_t> timeouts_before_sample;
	const auto task_events_before_sample = fake_task_events;
	idle_backend.before_finish = [&]
	{
		timeouts_before_sample = idle_backend.poll_timeouts;
		telemetry_sample sample{
			.sensor = {
				.sequence = 1,
				.temperature_c = 21,
				.pressure_pa = 100000,
				.valid = true
			}
		};
		require(idle_service.submit(sample).has_value());
		require(idle_service.on_advertising_requested().has_value());
	};
	const auto advertisements_before_request = idle_backend.advertisements;
	try
	{
		fake_task(fake_argument);
	}
	catch (const finished &)
	{
	}
	const auto idle_poll_waited_for_events = !timeouts_before_sample.empty() &&
		timeouts_before_sample.back() == bluetooth_config{}.idle_timeout_ms;
	const auto sample_woke_bluetooth_task = fake_task_events > task_events_before_sample;
	require(idle_poll_waited_for_events && sample_woke_bluetooth_task && !idle_backend.measurements.empty());
	require(idle_backend.advertisements == advertisements_before_request + 1);
	idle_service.stop();

	for (unsigned field = 0; field < 15; ++field)
	{
		bluetooth_config invalid;
		switch (field)
		{
			case 0: invalid.poll_timeout_ms = 0; break;
			case 1: invalid.reconnect_interval_ms = 0; break;
			case 2: invalid.command_capacity = 0; break;
			case 3: invalid.command_capacity = bluetooth_event{}.payload.size() + 1; break;
			case 4: invalid.reply_queue_capacity = 0; break;
			case 5: invalid.log_queue_capacity = 0; break;
			case 6: invalid.replies_per_cycle = 0; break;
			case 7: invalid.logs_per_cycle = 0; break;
			case 8: invalid.log_text_capacity = 0; break;
			case 9: invalid.log_text_capacity = bluetooth_config::log_text_storage_capacity; break;
			case 10: invalid.task_stack_depth = 0; break;
			case 11: invalid.task_priority = configMAX_PRIORITIES; break;
			case 12: invalid.reply_queue_capacity = static_cast<std::size_t>(UINT32_MAX) + 1; break;
			case 13: invalid.measurement_lifetime_ms = 0; break;
			case 14: invalid.idle_timeout_ms = 0; break;
		}
		bluetooth_service invalid_service{backend, commands, logger, monitor, invalid};
		const auto result = invalid_service.start();
		require(!result && result.error().code == error_code::invalid_config);
		require(logger.sink == nullptr);
	}
	fail_task_create = true;
	require(!service.start());
	require(!service.submit_reply({}));
	std::cout << "Commands, addressed replies, MTU chunks, stale reply rejection, telemetry and startup recovery passed\n";
}
