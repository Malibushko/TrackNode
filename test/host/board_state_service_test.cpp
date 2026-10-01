#include "infrastructure/board_state_service.h"
#include "application/board_voltage.h"
#include <cassert>
#include <cstdio>

int main()
{
	assert(supply_mv_from_vrefint(1500, 1500) == 3300);
	assert(supply_mv_from_vrefint(1650, 1500) == 3000);
	assert(!supply_mv_from_vrefint(0, 1500));
	assert(!supply_mv_from_vrefint(4095, 1500));
	assert(!supply_mv_from_vrefint(1500, 0));
	assert(!supply_mv_from_vrefint(1, 4094));
	assert(supply_is_low(3000, false, 3000, 3100));
	assert(supply_is_low(3099, true, 3000, 3100));
	assert(!supply_is_low(3100, true, 3000, 3100));

	board_state_service service;
	assert(!service.read());
	assert(service.sample_if_due(0));
	assert(service.read()->supply_mv == 3300);
	assert(adc::read_count == 16 && adc::init_count == 1);
	assert(service.sample_if_due(4999));
	assert(adc::read_count == 16);
	adc::raw = 1650;
	assert(service.sample_if_due(5000));
	assert(service.read()->low_voltage);
	adc::raw = 1600; // 3094 mV: do not chatter inside the hysteresis band.
	assert(service.sample_if_due(10000));
	assert(service.read()->low_voltage);
	adc::raw = 1590;
	assert(service.sample_if_due(15000));
	assert(!service.read()->low_voltage);
	adc::fail_read = true;
	assert(!service.sample_if_due(20000));
	assert(!service.read()); // Do not expose a stale value as a current good sample.
	const auto reads = adc::read_count;
	assert(service.sample_if_due(20001));
	assert(adc::read_count == reads); // Failed reads are rate limited too.
	adc::fail_read = false;
	adc::raw = 0;
	assert(!service.sample_if_due(25000));
	assert(!service.read());
	adc::raw = 1500;
	assert(service.sample_if_due(30000));
	assert(service.read()->supply_mv == 3300 && adc::init_count == 1);

	board_state_service wrapping;
	constexpr std::uint32_t start = UINT32_MAX - 100;
	assert(wrapping.sample_if_due(start));
	const auto before_wrap = adc::read_count;
	assert(wrapping.sample_if_due(start + 4999u));
	assert(adc::read_count == before_wrap);
	assert(wrapping.sample_if_due(start + 5000u));
	assert(adc::read_count == before_wrap + 16);
	board_state_service invalid{board_state_service::config{5000, 3100, 3000}};
	assert(!invalid.sample_if_due(0));
	std::puts("PASS: calibrated VDDA, cadence/wrap, hysteresis, failed sample recovery");
}
