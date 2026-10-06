#include "application/telemetry_router.h"
#include "application/radio_telemetry_filter.h"
#include <cassert>

struct transport final : telemetry_sink
{
	int calls = 0;
	bool reject = false;
	power_mode mode = power_mode::high;
	status submit(const telemetry_sample &) override
	{ ++calls; return reject ? status{fail(component::service, error_code::queue_full)} : status{}; }
	void set_power_mode(power_mode value) override { mode = value; }
};

int main()
{
	transport radio, ble;
	radio_telemetry_filter filtered_radio{radio};
	telemetry_router router;
	telemetry_sample sample{{0, 1, 20, 100000, true}, {}, 1000};
	assert(!router.submit(sample));
	assert(router.add(filtered_radio));
	assert(!router.add(filtered_radio));
	assert(router.add(ble)); router.seal();
	assert(!router.add(ble));
	router.set_power_mode(power_mode::low);
	assert(radio.mode == power_mode::low && ble.mode == power_mode::low);
	radio.reject = true;
	assert(!router.submit(sample)); assert(ble.calls == 1);
	radio.reject = false;
	sample.recorded_at = 1100;
	assert(router.submit(sample)); // Rejected sample must not suppress the retry.
	assert(radio.calls == 2 && ble.calls == 2);
	sample.recorded_at = 2100;
	assert(router.submit(sample)); assert(radio.calls == 2 && ble.calls == 3);
	sample.sensor.temperature_c = 21;
	assert(router.submit(sample)); assert(radio.calls == 3 && ble.calls == 4);
	sample.recorded_at += radio_publish_policy::heartbeat_ms;
	assert(router.submit(sample)); assert(radio.calls == 4);
	router.set_power_mode(power_mode::high);
	assert(router.submit(sample)); assert(radio.calls == 5);
	telemetry_router offline; offline.seal(); assert(offline.submit(sample));

	// A router forwards every sample even in low power; only wrappers filter.
	transport first, second;
	telemetry_router plain;
	assert(plain.add(first)); assert(plain.add(second)); plain.seal();
	plain.set_power_mode(power_mode::low);
	first.reject = true;
	assert(!plain.submit(sample)); assert(!plain.submit(sample));
	assert(first.calls == 2 && second.calls == 2);

	// Each wrapper tracks acceptance independently, including rejected retries.
	transport left, right;
	radio_telemetry_filter left_filter{left}, right_filter{right};
	telemetry_router independent;
	assert(independent.add(left_filter)); assert(independent.add(right_filter));
	independent.seal(); independent.set_power_mode(power_mode::low);
	right.reject = true;
	assert(!independent.submit(sample));
	right.reject = false;
	assert(independent.submit(sample));
	assert(left.calls == 1 && right.calls == 2);
}
