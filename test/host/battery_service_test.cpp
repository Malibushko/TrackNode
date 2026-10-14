#include "infrastructure/battery_service.h"
#include "application/battery_voltage.h"
#include "application/board_voltage.h"
#include <cassert>
#include <cstdio>

int main()
{
	assert(supply_mv_from_vrefint(1500, 1500) == 3300);
	assert(supply_mv_from_vrefint(1650, 1500) == 3000);
	assert(!supply_mv_from_vrefint(0, 1500));
	assert(!supply_mv_from_vrefint(1500, 0));
	assert(battery_mv_from_adc(2470, 3300, 10000, 10000) == 3981);
	assert(battery_mv_from_adc(2716, 3000, 10000, 10000) == 3979);
	assert(battery_mv_from_adc(2048, 3300, 20000, 10000) == 4951);
	assert(!battery_mv_from_adc(0, 3300, 10000, 10000));
	assert(!battery_mv_from_adc(4095, 3300, 10000, 10000));
	assert(!battery_mv_from_adc(1000, 3300, 10000, 0));
	assert(!battery_mv_from_adc(4000, UINT16_MAX, UINT32_MAX, 1));
	assert(estimate_battery_level(2500) == 0);
	assert(estimate_battery_level(3980) == 72);
	assert(estimate_battery_level(4200) == 100);
	assert(estimate_battery_level(4500) == 100);
	assert(valid_battery_profile(provisional_lipo_profile));
	const std::array<battery_level_point, 3> calibrated{{
		{.mv = 3200,
		 .percent = 0},
		{.mv = 3800,
		 .percent = 50},
		{.mv = 4200,
		 .percent = 100}
	}};
	assert(estimate_battery_level(3500, calibrated) == 25);
	assert(estimate_battery_level(4000, calibrated) == 75);
	auto broken = calibrated;
	broken[1].mv = broken[0].mv;
	assert(!valid_battery_profile(broken));
	broken = calibrated;
	broken[1].percent = 101;
	assert(!valid_battery_profile(broken));
	assert(!valid_battery_profile({}));
	for (std::uint16_t mv = 2501; mv < 4500; ++mv)
	{
		assert(estimate_battery_level(mv) >= estimate_battery_level(mv - 1));
	}
	assert(supply_is_low(3300, false, 3300, 3500));
	assert(supply_is_low(3499, true, 3300, 3500));
	assert(!supply_is_low(3500, true, 3300, 3500));

	battery_service::config settings{
		.battery_input = adc_config{
			.channel = ADC_CHANNEL_1,
			.sample_time = ADC_SAMPLETIME_480CYCLES
		}
	};
	battery_service service{settings};
	assert(!service.read());
	assert(service.sample_if_due(0));
	assert(service.read()->battery_mv == 3981);
	assert(service.read()->supply_mv == 3300);
	assert(service.read()->level_percent_estimate == 72);
	assert(!service.read()->usb_present);
	assert(service.read()->battery_min_raw == 2470);
	assert(service.read()->battery_max_raw == 2470);
	assert(adc::read_count == 34 && adc::init_count == 1);
	assert((adc::selected_channels == std::vector<std::uint32_t>{17, 1}));
	assert(service.sample_if_due(4999));
	assert(adc::read_count == 34);

	adc::reference_raw = 1650;
	adc::battery_raw = 2716;
	gpio::usb_present = true;
	assert(service.sample_if_due(5000));
	assert(service.read()->supply_mv == 3000);
	assert(service.read()->battery_mv == 3979);
	assert(service.read()->usb_present);
	assert(service.read()->low_supply);
	assert(adc::init_count == 1);

	adc::fail_channel = ADC_CHANNEL_1;
	assert(!service.sample_if_due(10000));
	assert(!service.read());
	const auto failed_reads = adc::read_count;
	assert(service.sample_if_due(10001));
	assert(adc::read_count == failed_reads);
	adc::fail_channel = ADC_CHANNEL_VREFINT;
	assert(!service.sample_if_due(15000));
	assert(!service.read());
	adc::fail_channel = UINT32_MAX;
	adc::battery_raw = 4095;
	assert(!service.sample_if_due(20000));
	assert(!service.read());
	adc::reference_raw = 1500;
	adc::battery_raw = 0;
	assert(!service.sample_if_due(25000));
	assert(!service.read());
	adc::battery_raw = 1000;
	assert(!service.sample_if_due(30000));
	adc::battery_raw = 2047;
	assert(service.sample_if_due(35000));
	assert(service.read()->low_battery);
	adc::battery_raw = 2110;
	assert(service.sample_if_due(40000));
	assert(service.read()->low_battery);
	adc::battery_raw = 2172;
	gpio::usb_present = false;
	assert(service.sample_if_due(45000));
	assert(!service.read()->low_battery);
	assert(!service.read()->low_supply);
	assert(!service.read()->usb_present);

	adc::battery_samples = {2400, 2600};
	assert(service.sample_if_due(50000));
	assert(service.read()->battery_raw == 2500);
	assert(service.read()->battery_min_raw == 2400);
	assert(service.read()->battery_max_raw == 2600);
	adc::battery_samples.clear();

	adc::reference_raw = 1500;
	adc::battery_raw = 2470;
	battery_service robust{settings};
	assert(robust.sample_if_due(0));
	adc::battery_index = 0;
	adc::battery_samples.assign(17, 2470);
	adc::battery_samples[1] = 3000;
	adc::battery_samples[2] = 3000;
	assert(robust.sample_if_due(5000));
	assert(robust.read()->battery_mv == 3981);
	assert(robust.read()->battery_raw > 2470);
	assert(robust.read()->battery_max_raw == 3000);
	adc::battery_samples.clear();
	adc::battery_raw = 2600;
	assert(robust.sample_if_due(10000));
	assert(robust.read()->battery_mv == 3981);
	adc::battery_raw = 2470;
	assert(robust.sample_if_due(15000));
	assert(robust.read()->battery_mv == 3981);
	adc::fail_channel = ADC_CHANNEL_1;
	assert(!robust.sample_if_due(20000));
	assert(!robust.read());
	adc::fail_channel = UINT32_MAX;
	adc::battery_raw = 2300;
	assert(robust.sample_if_due(25000));
	assert(robust.read()->battery_mv == *battery_mv_from_adc(2300, 3300, 10000, 10000));

	battery_service wrapping{settings};
	constexpr std::uint32_t start = UINT32_MAX - 100;
	assert(wrapping.sample_if_due(start));
	const auto before_wrap = adc::read_count;
	assert(wrapping.sample_if_due(start + 4999u));
	assert(adc::read_count == before_wrap);
	assert(wrapping.sample_if_due(start + 5000u));
	assert(adc::read_count == before_wrap + 34);
	settings.lower_ohms = 0;
	battery_service invalid{settings};
	assert(!invalid.sample_if_due(0));
	settings.lower_ohms = 10000;
	settings.level_profile = broken;
	battery_service invalid_profile{settings};
	assert(!invalid_profile.sample_if_due(0));

	battery_estimator estimator;
	assert(estimator.update(3900, 0, false).mv == 3900);
	assert(estimator.update(4200, 5000, false).mv == 3900);
	assert(estimator.update(3900, 10000, false).mv == 3900);
	assert(estimator.update(3600, 15000, false).mv == 3900);
	assert(estimator.update(3900, 20000, false).mv == 3900);
	std::uint16_t last = 3900;
	for (std::uint32_t time = 25000; time <= 200000; time += 5000)
	{
		const auto next = estimator.update(3700, time, false);
		assert(next.mv <= last && next.mv >= 3700);
		last = next.mv;
	}
	assert(last == 3700);
	assert(estimator.update(4200, 205000, true).mv == 4200);
	assert(estimator.update(3900, 210000, false).mv == 3900);
	assert(estimator.update(3800, 225000, false).mv == 3800);
	estimator.reset();
	assert(estimator.update(3500, UINT32_MAX - 100, false).mv == 3500);
	assert(estimator.update(4200, 4899, false).mv == 3500);

	battery_estimator hysteresis;
	assert(hysteresis.update(3900, 0, false).percent == 60);
	for (std::uint32_t time = 5000; time <= 60000; time += 5000)
	{
		assert(hysteresis.update(3906, time, false).percent == 60);
	}
	assert(hysteresis.update(4200, 65000, true).percent == 100);
	assert(hysteresis.update(3300, 70000, false).percent == 0);

	constexpr std::array<std::uint16_t, 26> autonomous_mv{{
		3928, 3924, 3952, 3954, 3937, 3933, 3918, 3934, 3927, 3904, 3913,
		3912, 3921, 3913, 3926, 3938, 3914, 3916, 3905, 3902, 3910, 3909,
		3917, 3931, 3931, 3910
	}};
	battery_estimator replay;
	std::uint16_t filtered_min = UINT16_MAX;
	std::uint16_t filtered_max = 0;
	std::uint32_t time = 0;
	for (const auto mv : autonomous_mv)
	{
		const auto next = replay.update(mv, time, false);
		filtered_min = std::min(filtered_min, next.mv);
		filtered_max = std::max(filtered_max, next.mv);
		time += 5000;
	}
	assert(filtered_max - filtered_min < 30);
	std::printf("Battery-only log replay: raw 3902..3954, filtered %u..%u mV\n",
	    filtered_min, filtered_max);
	std::puts("PASS: battery/VDDA calibration, channel switching, USB, cadence/wrap, failures and recovery");
}
