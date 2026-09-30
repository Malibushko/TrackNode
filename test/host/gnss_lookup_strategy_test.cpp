#include "infrastructure/gnss_lookup_strategy.h"

#include <cassert>
#include <cstdint>

int main()
{
	gnss_lookup_strategy lookup;
	lookup.start(1'000);
	assert(lookup.should_lookup(1'000));
	assert(lookup.lookup_remaining_ms(1'000) == 30'000);
	assert(lookup.should_lookup(30'999));
	assert(lookup.should_sleep(31'000));
	assert(lookup.sleep_duration_ms() == 120'000);

	lookup.on_sleep_started(31'000);
	assert(lookup.sleeping());
	assert(lookup.attempt_index() == 1);
	assert(lookup.sleep_remaining_ms(31'000) == 120'000);
	assert(lookup.sleep_remaining_ms(151'000) == 0);
	lookup.on_wake(151'000);
	assert(lookup.lookup_remaining_ms(151'000) == 45'000);
	assert(lookup.should_sleep(196'000));
	assert(lookup.sleep_duration_ms() == 300'000);

	lookup.on_sleep_started(196'000);
	lookup.on_wake(496'000);
	assert(lookup.lookup_remaining_ms(496'000) == 60'000);
	lookup.on_sleep_started(556'000);
	assert(lookup.attempt_index() == 2);
	assert(lookup.sleep_remaining_ms(556'000) == 900'000);

	lookup.on_wake(600'000); // on-demand wake during backoff
	assert(lookup.lookup_remaining_ms(600'000) == 60'000);
	lookup.on_fix();
	assert(lookup.should_sleep(600'001));
	assert(lookup.sleep_duration_ms() == 300'000);
	lookup.on_sleep_started(600'001);
	assert(lookup.attempt_index() == 0);
	lookup.on_wake(900'001);
	assert(lookup.lookup_remaining_ms(900'001) == 30'000);

	const std::uint32_t near_wrap = UINT32_MAX - 100;
	lookup.start(near_wrap);
	assert(lookup.should_lookup(near_wrap + 29'999));
	assert(lookup.should_sleep(near_wrap + 30'000));
}
