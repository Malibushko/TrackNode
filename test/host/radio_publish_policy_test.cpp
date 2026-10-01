#include "application/radio_publish_policy.h"

#include <cassert>
#include <cstdint>

int main()
{
	radio_publish_policy policy;
	assert(policy.should_publish(20.0f, 100'000.0f, 1'000));
	// A full queue leaves the policy unchanged, so the next sample may retry.
	assert(policy.should_publish(20.0f, 100'000.0f, 1'500));
	policy.on_published(20.0f, 100'000.0f, 1'000);

	assert(!policy.should_publish(20.5f, 100'000.0f, 1'999));
	assert(!policy.should_publish(20.49f, 100'999.0f, 2'000));
	assert(policy.should_publish(20.5f, 100'000.0f, 2'000));
	policy.on_published(20.5f, 100'000.0f, 2'000);
	assert(policy.should_publish(20.0f, 100'000.0f, 3'000));
	policy.on_published(20.0f, 100'000.0f, 3'000);
	assert(policy.should_publish(20.0f, 101'000.0f, 4'000));
	policy.on_published(20.0f, 101'000.0f, 4'000);

	assert(!policy.should_publish(20.0f, 101'000.0f, 303'999));
	assert(policy.should_publish(20.0f, 101'000.0f, 304'000));

	const std::uint32_t near_wrap = UINT32_MAX - 500;
	policy.on_published(20.0f, 101'000.0f, near_wrap);
	assert(!policy.should_publish(20.5f, 101'000.0f, near_wrap + 999));
	assert(policy.should_publish(20.5f, 101'000.0f, near_wrap + 1'000));
}
