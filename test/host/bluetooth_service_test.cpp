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
	unsigned initialized = 0;
	unsigned advertisements = 0;
	bool fail_init = false;
	bool busy_advertise = false;

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
	result<bluetooth_event> poll(std::uint32_t) override
	{
		if (events.empty())
		{
			if (before_finish)
			{
				auto callback = std::move(before_finish);
				callback();
				return bluetooth_event{};
			}

			throw finished{};
		}

		const auto event = events.front();
		events.pop_front();
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
	fail_task_create = true;
	require(!service.start());
	require(!service.submit_reply({}));
	std::cout << "Commands, addressed replies, MTU chunks, stale reply rejection, telemetry and startup recovery passed\n";
}
