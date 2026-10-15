#include "infrastructure/battery_service.h"
#include "application/battery_schedule.h"
#include "application/board_voltage.h"
#include <cassert>
#include <cstdio>

int main()
{
	assert(supply_mv_from_vrefint(1500, 1500) == 3300);
	assert(!supply_mv_from_vrefint(0, 1500));
	assert(battery_mv_from_adc(2470, 3300, 10000, 10000) == 3981);
	assert(!battery_mv_from_adc(4095, 3300, 10000, 10000));
	assert(!battery_mv_from_adc(4000, UINT16_MAX, UINT32_MAX, 1));
	assert(valid_battery_profile(provisional_lipo_profile));
	assert(!valid_battery_profile({}));
	for (std::uint16_t mv = 2501; mv < 4500; ++mv)
	{
		assert(estimate_battery_level(mv) >= estimate_battery_level(mv - 1));
	}

	battery_schedule schedule;
	schedule.poll(0);
	assert(schedule.pending() && !schedule.ready(0, false));
	schedule.request(30000);
	assert(!schedule.ready(59999, false));
	assert(schedule.ready(60000, false));
	assert(schedule.ready(1, true));
	schedule.complete(90000, true);
	schedule.poll(389999);
	assert(!schedule.pending());
	schedule.poll(390000);
	assert(schedule.ready(390000, true));
	schedule.complete(420000, false);
	schedule.poll(449999);
	assert(!schedule.pending());
	schedule.poll(450000);
	assert(schedule.pending());
	battery_schedule wrapping;
	wrapping.request(UINT32_MAX - 100);
	assert(!wrapping.ready(59898, false));
	assert(wrapping.ready(59899, false));
	battery_schedule custom_schedule{battery_schedule::config{
		.update_interval_ms = 100,
		.retry_interval_ms = 20,
		.low_load_wait_ms = 10
	}};
	custom_schedule.poll(0);
	assert(!custom_schedule.ready(9, false));
	assert(custom_schedule.ready(10, false));
	custom_schedule.complete(10, true);
	custom_schedule.poll(109);
	assert(!custom_schedule.pending());
	custom_schedule.poll(110);
	assert(custom_schedule.pending());
	custom_schedule.complete(110, false);
	custom_schedule.poll(129);
	assert(!custom_schedule.pending());
	custom_schedule.poll(130);
	assert(custom_schedule.pending());

	battery_service::config settings{
		.battery_input = adc_config{
			.channel = ADC_CHANNEL_1,
			.sample_time = ADC_SAMPLETIME_480CYCLES
		}
	};
	battery_service service{settings};
	assert(!service.read());
	assert(service.poll_power());
	service.begin_series(0, false);
	assert(service.current().update_pending);
	assert(service.sample_step(0, false));
	const auto reads = adc::read_count;
	assert(!service.read());
	assert(service.sample_step(999, false));
	assert(adc::read_count == reads);
	for (std::uint32_t now = 1000; now < 30000; now += 1000)
	{
		assert(service.sample_step(now, false));
	}
	assert(!service.sampling());
	assert(service.read()->battery_mv == 3981);
	assert(service.read()->level_percent_estimate == 72);
	assert(service.read()->quality == 1);
	assert(service.read()->sampled_at_ms == 29000);
	assert(adc::read_count == 30 * 34 && adc::init_count == 1);
	const auto cached = service.read();
	assert(service.read() && adc::read_count == 30 * 34);

	const auto series = [](battery_service &battery, std::uint32_t start, bool loaded)
	{
		assert(battery.poll_power());
		battery.begin_series(start, loaded);
		for (std::uint32_t index = 0; index < 30; ++index)
		{
			assert(battery.sample_step(start + index * 1000, loaded));
		}
	};
	adc::battery_samples = {2400, 2600};
	series(service, 330000, false);
	assert(service.current().unstable == false);
	assert(service.current().battery_raw == 2500);
	assert(service.current().battery_min_raw == 2400 && service.current().battery_max_raw == 2600);
	adc::battery_samples.clear();

	const auto last_good = service.read()->sampled_at_ms;
	service.begin_series(400000, false);
	for (std::uint32_t index = 0; index < 30; ++index)
	{
		adc::battery_raw = index % 2 ? 2400 : 2600;
		assert(service.sample_step(400000 + index * 1000, false));
	}
	assert(service.current().unstable && service.needs_retry());
	assert(service.read()->sampled_at_ms == last_good);
	assert(service.read()->quality == 3);
	adc::fail_channel = ADC_CHANNEL_1;
	service.begin_series(500000, false);
	assert(!service.sample_step(500000, false));
	assert(!service.sampling() && service.read()->sampled_at_ms == last_good);
	adc::fail_channel = UINT32_MAX;

	adc::battery_raw = 2470;
	series(service, 600000, true);
	assert(service.read()->quality == 2 && !service.current().unstable);
	service.begin_series(700000, false);
	assert(service.sample_step(700000, false));
	gpio::usb_present = true;
	const auto edge = service.poll_power();
	assert(edge && *edge && !service.sampling());
	assert(service.read()->usb_present && service.read()->sampled_at_ms == 629000);
	assert(service.current().update_pending);
	series(service, 710000, false);
	service.mark_loaded();
	assert(service.read()->quality == 2);

	battery_service low{settings};
	assert(low.poll_power());
	adc::battery_raw = 2000;
	low.begin_series(0, true);
	assert(low.sample_step(0, true));
	assert(low.sample_step(1000, true));
	assert(!low.current().low_battery);
	assert(low.sample_step(2000, true));
	assert(low.current().low_battery && !low.read());
	adc::battery_raw = 2470;
	for (std::uint32_t index = 3; index < 30; ++index)
	{
		assert(low.sample_step(index * 1000, true));
	}
	assert(!low.current().low_battery);

	battery_estimator estimator;
	assert(estimator.update(3900, 0, false).percent == 60);
	const auto spike = estimator.update(3700, 300000, false);
	assert(spike.percent == 60 && spike.confirming && !spike.reanchored);
	assert(estimator.update(3900, 330000, false).percent == 60);
	assert(estimator.update(3700, 360000, false).confirming);
	assert(estimator.update(3700, 390000, false).confirming);
	const auto confirmed = estimator.update(3700, 420000, false);
	assert(confirmed.percent == 20 && confirmed.reanchored);
	battery_estimator fractional;
	assert(fractional.update(3900, 0, false).percent == 60);
	assert(fractional.update(3890, 300000, false).percent == 60);
	assert(fractional.update(3890, 600000, false).percent == 59);
	const auto source_edge = fractional.update(4200, 601000, true);
	assert(source_edge.percent == 59 && source_edge.confirming);
	fractional.interrupt_confirmation();
	assert(fractional.update(4200, 631000, true).confirming);

	settings.sample_interval_ms = 2000;
	battery_service slower{settings};
	assert(slower.poll_power());
	slower.begin_series(0, false);
	assert(slower.sample_step(0, false));
	const auto slower_reads = adc::read_count;
	assert(slower.sample_step(1999, false));
	assert(adc::read_count == slower_reads);
	assert(slower.sample_step(2000, false));
	assert(adc::read_count == slower_reads + 34);
	battery_estimator custom_estimator{battery_estimator::config{
		.rate_period_ms = 1000
	}};
	assert(custom_estimator.update(3900, 0, false).percent == 60);
	assert(custom_estimator.update(3890, 1000, false).percent == 60);
	assert(custom_estimator.update(3890, 2000, false).percent == 59);
	settings.sample_interval_ms = 0;
	battery_service invalid_interval{settings};
	assert(!invalid_interval.poll_power());
	settings.sample_interval_ms = 1000;
	settings.estimator.rate_period_ms = 0;
	battery_service invalid_estimator{settings};
	assert(!invalid_estimator.poll_power());
	settings.estimator.rate_period_ms = 300000;
	settings.lower_ohms = 0;
	battery_service invalid{settings};
	assert(!invalid.poll_power());
	std::puts("PASS: background cadence/deadline/coalescing, nonblocking series, cached reads, unstable/error retention, USB edge, low voltage and UX reanchor");
}
