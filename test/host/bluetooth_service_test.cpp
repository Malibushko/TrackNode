#include "infrastructure/bluetooth_service.h"
#include "infrastructure/logger_service.h"
#include "infrastructure/supervisor.h"
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
	std::function<void()> before_finish;
	std::function<void(const bluetooth_event &)> before_event;
	unsigned initialized = 0;
	unsigned advertisements = 0;
	std::uint32_t last_poll_timeout_ms = 0;
	bool fail_init = false;
	bool busy_advertise = false;
	bool restart_poll = false;

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

	require(commands.received.size() == 1);
	require(commands.received[0].value.type == command_type::ping);
	require(commands.received[0].session == 10 && commands.received[0].reply_to == &service);
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
	require(fake_ticks - initial_ticks == 37 && custom_backend.last_poll_timeout_ms == 7);
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

	for (unsigned field = 0; field < 13; ++field)
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
