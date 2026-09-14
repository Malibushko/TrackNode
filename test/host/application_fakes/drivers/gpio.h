#pragma once
#include "core/result.h"
#include <vector>

class gpio
{
public:
	enum class state
	{
		set,
		reset
	};
	std::vector<state> writes;
	bool fail_write = false;

	status write(state value)
	{
		if (fail_write)
		{
			return fail(component::gpio, error_code::not_initialized);
		}
		writes.push_back(value);
		return {};
	}
};
