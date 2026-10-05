#include "application/telemetry_router.h"
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
	telemetry_router router;
	telemetry_sample sample{{0, 1, 20, 100000, true}, {}, 1000};
	assert(!router.submit(sample));
	assert(router.add(radio, telemetry_policy::on_change_in_low_power));
	assert(!router.add(radio));
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
}
