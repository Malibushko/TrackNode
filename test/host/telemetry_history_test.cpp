#include "infrastructure/telemetry_journal_service.h"
#include "libs/freertos/freertos_helpers.h"
#include <cassert>
#include <string>

class live_sink final : public telemetry_sink
{
public:
	unsigned received = 0;
	status submit(const telemetry_sample &) override
	{
		++received;
		return fail(component::service, error_code::not_ready);
	}
	void set_power_mode(power_mode) override {}
};

static telemetry_record environment(sequence_t id, std::int16_t temperature, std::uint32_t pressure, std::uint32_t seconds)
{
	telemetry_record record;
	record.set_identity(id, seconds);
	record.set_environment(temperature, pressure);
	return record;
}

//////////////////////////////////////////////////////////////////////////

static void check_storage_and_policy()
{
	telemetry_archive_policy policy;
	const auto first = environment(1, -100, 100000, 0);
	assert(policy.should_archive(first));
	policy.on_archived(first);
	assert(!policy.should_archive(environment(2, -75, 100050, 1)));
	assert(policy.should_archive(environment(3, -50, 100000, 2)));
	assert(policy.should_archive(environment(4, -100, 100100, 2)));
	assert(policy.should_archive(environment(5, -100, 100000, 3600)));
	telemetry_record invalid;
	invalid.set_identity(6, 3);
	assert(policy.should_archive(invalid));
	policy.on_archived(invalid);
	assert(!policy.should_archive(invalid));
	assert(policy.should_archive(first));

	auto position = environment(7, -100, 100000, 10);
	position.set_position(10, 0, 1799999900);
	policy.on_archived(position);
	position.set_position(11, 0, -1799999900);
	assert(!policy.should_archive(position));
	position.set_position(12, 100000, -1799999900);
	assert(policy.should_archive(position));

	telemetry_journal<3> journal;
	for (sequence_t index = 1; index <= 5; ++index)
	{
		assert(journal.append(environment(index * 10, static_cast<std::int16_t>(index), 100000, static_cast<std::uint32_t>(index))));
	}
	assert(journal.summary().oldest_sample_id == 30 && journal.summary().newest_sample_id == 50);
	assert(journal.summary().record_count == 3 && journal.summary().overwritten_count == 2);
	assert(!journal.append(environment(50, 1, 1, 1)));
	const auto page = journal.read(30, 50);
	assert(page && page->count == 2 && page->records[0].sample_id() == 40);
	assert(journal.summary().record_count == 3);
	assert(!journal.read(50, 30));
	journal.truncate_through(35);
	assert(journal.summary().oldest_sample_id == 40 && journal.summary().record_count == 2);
	journal.truncate_through(35);
	assert(journal.summary().record_count == 2);
	journal.clear();
	assert(journal.summary().record_count == 0 && journal.summary().newest_sample_id == 0);
	assert(!journal.append(environment(1, 1, 1, 1)));
	assert(journal.append(environment(60, 1, 1, 1)));
}

//////////////////////////////////////////////////////////////////////////

static void check_parser()
{
	assert(telemetry_history_protocol::can_handle_command("H"));
	assert(telemetry_history_protocol::can_handle_command("H R 000000000000002A"));
	assert(!telemetry_history_protocol::can_handle_command("PING"));
	const auto read = telemetry_history_protocol::parse_command("H R 000000000000002A");
	assert(read && read->operation == telemetry_history_operation::read_page && read->value == 42);
	assert(!telemetry_history_protocol::parse_command("H O 00000000000000GG"));
	assert(!telemetry_history_protocol::parse_command("H Z 000000000000002A"));
	assert(!telemetry_history_protocol::parse_command("H"));
}

//////////////////////////////////////////////////////////////////////////

static void check_response_encoding()
{
	telemetry_history_protocol::response_buffer buffer;
	buffer.fill('!');
	const telemetry_history_snapshot snapshot{
		.device_id = {0x01234567, 0x89abcdef, 0xffffffff},
		.boot_epoch = 0xffffffffffffffffULL,
		.uptime_seconds = 0xffffffff,
		.oldest_sample_id = 1,
		.last_sample_id = 0xffffffffffffffffULL,
		.record_count = 1024,
		.overwritten_count = 0xffffffffffffffffULL
	};
	const auto encoded = telemetry_history_protocol::encode_snapshot(buffer, snapshot);
	assert(encoded == "H O 2 0123456789ABCDEFFFFFFFFF FFFFFFFFFFFFFFFF FFFFFFFF 0000000000000001 FFFFFFFFFFFFFFFF 00000400 FFFFFFFFFFFFFFFF\r\n");
	assert(buffer[encoded.size()] == 0 && encoded.size() < buffer.size());
	assert(telemetry_history_protocol::encode_page_begin(buffer, {
		.after_sample_id = 1,
		.last_sample_id = 42,
		.record_count = 8
	}) == "H B 0000000000000001 000000000000002A 00000008\r\n");
	telemetry_record record;
	record.bytes.fill(255);
	assert(telemetry_history_protocol::encode_record(buffer, record) == "H D FFFFFFFFFFFFFFFF " + std::string(62, 'F') + "\r\n");
	assert(telemetry_history_protocol::encode_page_end(buffer, 0) == "H E 0000000000000000\r\n");
	assert(std::string_view(buffer.data()) == "H E 0000000000000000\r\n");
	assert(telemetry_history_protocol::encode_acknowledgement(buffer, 42) == "H A 000000000000002A\r\n");
	assert(telemetry_history_protocol::error_response(telemetry_history_error::records_overwritten) == "ERR HISTORY_GAP\r\n");
}

//////////////////////////////////////////////////////////////////////////

static void check_service()
{
	live_sink live;
	telemetry_journal_service service{live};
	telemetry_sample sample;
	sample.id = 42;
	sample.sensor.valid = true;
	sample.sensor.temperature_c = -12.34f;
	sample.sensor.pressure_pa = 100000;
	sample.sensor.timestamp = 2000;
	fake_ticks = 3000;
	assert(!service.submit(sample));
	assert(live.received == 1);
	const auto parsed = telemetry_history_protocol::parse_command("H O 000000000000002A");
	assert(parsed);
	assert(service.handle_history_command("H O 0000000000000000", 1).find("ERR HISTORY_NONCE") == 0);
	assert(service.handle_history_command("H O 000000000000002A", 1) ==
		"H O 2 000000000000000000000000 000000000000002A 00000003 000000000000002A 000000000000002A 00000001 0000000000000000\r\n");
	assert(service.handle_history_command("H A 000000000000002A", 1).find("ERR HISTORY_ACK") == 0);
	assert(service.handle_history_command("H R 0000000000000000", 2).find("ERR HISTORY_NOT_OPEN") == 0);
	assert(service.handle_history_command("H R 0000000000000000", 1).empty());
	assert(service.next_history_response() == "H B 0000000000000000 000000000000002A 00000001\r\n");
	service.on_history_response_sent();
	assert(service.handle_history_command("H A 000000000000002A", 1).find("ERR HISTORY_BUSY") == 0);
	assert(service.next_history_response() == "H D 000000000000002A 2A000000000000000200000000000000012EFBA08601000000000000000000\r\n");
	service.on_history_response_sent();
	assert(service.next_history_response() == "H E 000000000000002A\r\n");
	service.on_history_response_sent();

	sample.id = 43;
	sample.sensor.temperature_c = -12.2f;
	(void)service.submit(sample);
	sample.id = 100;
	sample.sensor.temperature_c = 20;
	(void)service.submit(sample);
	assert(service.handle_history_command("H A 0000000000000064", 1).find("ERR HISTORY_ACK") == 0);
	assert(service.handle_history_command("H A 000000000000002A", 1) == "H A 000000000000002A\r\n");
	assert(service.handle_history_command("H A 000000000000002A", 1) == "H A 000000000000002A\r\n");
	service.reset_history_transfer();
	assert(service.handle_history_command("H A 000000000000002A", 1).find("ERR HISTORY_NOT_OPEN") == 0);
	assert(service.handle_history_command("H O 0000000000000044", 3).find("000000000000002A 00000003 0000000000000064 0000000000000064 00000001") != std::string_view::npos);
	for (unsigned index = 0; index < 1025; ++index)
	{
		sample.id++;
		sample.sensor.temperature_c = index % 2 ? 10 : 20;
		(void)service.submit(sample);
	}
	assert(service.handle_history_command("H R 0000000000000000", 3).find("ERR HISTORY_GAP") == 0);

	live_sink other_live;
	telemetry_journal_service other{other_live};
	fake_ticks = 0xfffffff0;
	const std::string before{other.handle_history_command("H O 0000000000000044", 1)};
	fake_ticks = 2000;
	const std::string after{other.handle_history_command("H O 0000000000000044", 1)};
	assert(before != after);
	assert(other_live.acknowledge(1).error().code == error_code::not_implemented);
	assert(!other.acknowledge(0));
}

//////////////////////////////////////////////////////////////////////////

int main()
{
	check_storage_and_policy();
	check_parser();
	check_response_encoding();
	check_service();
}
