#pragma once

#include "application/telemetry_sink.h"
#include <array>

template<std::size_t N>
class telemetry_router final : public telemetry_sink
{
public:
	status add(telemetry_sink &sink)
	{
		if (sealed_ || count_ == entries_.size())
			return fail(component::service, error_code::invalid_config);

		for (std::size_t i = 0; i < count_; ++i)
			if (entries_[i] == &sink)
				return fail(component::service, error_code::invalid_config);

		entries_[count_++] = &sink;
		return {};
	}

	void seal() { sealed_ = true; }

	status submit(const telemetry_sample &sample) override
	{
		if (!sealed_)
			return fail(component::service, error_code::not_initialized);

		status result{};

		for (std::size_t i = 0; i < count_; ++i)
		{
			if (auto accepted = entries_[i]->submit(sample); !accepted && result)
				result = tl::make_unexpected(accepted.error());
		}

		return result;
	}

	void set_power_mode(power_mode mode) override
	{
		for (std::size_t i = 0; i < count_; ++i)
			entries_[i]->set_power_mode(mode);
	}

private:
	std::array<telemetry_sink *, N> entries_{};
	std::size_t count_ = 0;
	bool sealed_ = false;
};
