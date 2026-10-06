#pragma once

#include "application/command_sink.h"

class deferred_command_sink final : public command_sink
{
public:
	status bind(command_sink &target)
	{
		if (target_ || &target == this)
		{
			return fail(component::service, error_code::invalid_config);
		}

		target_ = &target;

		return {};
	}

	using command_sink::submit;
	status submit(command_request request) override
	{
		if (!target_)
		{
			return fail(component::service, error_code::not_initialized);
		}

		return target_->submit(request);
	}

	[[nodiscard]] bool bound() const
	{
		return target_ != nullptr;
	}

private:
	command_sink *target_ = nullptr;
};
