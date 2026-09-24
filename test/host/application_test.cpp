#include "infrastructure/status_indicator.h"
#include "infrastructure/track_node_control_protocol.h"
#include <cassert>
#include <cstdio>
#include <vector>

struct input
{
	std::vector<uint8_t> bytes;
	size_t position = 0;

	uint8_t read()
	{
		return bytes[position++];
	}

	size_t available()
	{
		return bytes.size() - position;
	}
};

int main()
{
	gpio led;
	status_indicator indicator{ led };
	assert(indicator.update(0, program_state::ok));
	assert(led.writes.back() == gpio::state::set);
	indicator.start_blink(2, 10);
	assert(indicator.update(10, program_state::ok));
	assert(led.writes.back() == gpio::state::reset);
	for (uint32_t now = 160; now <= 610; now += 150)
	{
		assert(indicator.update(now, program_state::ok));
	}
	assert(indicator.busy());
	assert(indicator.update(760, program_state::ok));
	assert(!indicator.busy());
	assert(led.writes.size() == 7);
	assert(led.writes.back() == gpio::state::set);

	const uint32_t start = UINT32_MAX - 50;
	indicator.start_blink(1, start);
	assert(indicator.update(start, program_state::ok));
	const size_t writes_before = led.writes.size();
	assert(indicator.update(start + 149u, program_state::ok));
	assert(led.writes.size() == writes_before);
	assert(indicator.update(start + 150u, program_state::ok));
	assert(led.writes.size() == writes_before + 1);

	assert(indicator.update(200, program_state::hal_error));
	assert(indicator.update(350, program_state::hal_error));
	assert(led.writes.back() == gpio::state::reset);
	indicator.start_blink(3, 351);
	assert(indicator.update(500, program_state::hal_error));
	assert(led.writes.back() == gpio::state::reset);
	assert(indicator.update(1500, program_state::hal_error));
	assert(led.writes.back() == gpio::state::set);
	led.fail_write = true;
	assert(!indicator.update(1650, program_state::hal_error));

	input stream;
	track_node_control_protocol protocol{ stream };
	stream.bytes = { 'x', 'p', 't', 'c', 'l' };
	assert(!protocol.process_next_byte());
	assert(!protocol.available());
	assert(protocol.process_next_byte());
	assert(protocol.process_next_byte());
	assert(protocol.process_next_byte());
	assert(protocol.process_next_byte());
	assert(stream.position == stream.bytes.size());
	assert(protocol.read() == command::ping);
	assert(protocol.read() == command::read_temperature);
	assert(protocol.read() == command::read_pressure);
	assert(protocol.read() == command::read_log);
	assert(!protocol.available());
	std::puts("PASS: blink sequence, wraparound, error priority, GPIO failure, command decoding");
}
