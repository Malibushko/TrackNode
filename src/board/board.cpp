#include "board/board.h"
#include "board/clock.h"
#include "platform/stdio.h"
#include "core/result.h"

#include <cstdio>
#include <ranges>

status board::init()
{
	if (attempted_)
	{
		return fail(component::board, error_code::already_initialized);
	}

	attempted_ = true;

	status result{};

	for (auto &name : insertion_order_)
	{
		component_t &component = components_.at(name);
		std::visit([&result](auto &device)
		{
			if (const status status = device.init())
			{
				result = status;
			}
		}, component);
	}

	return result;
}