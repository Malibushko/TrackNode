#include "board/board.h"

status board::init()
{
	if (attempted_)
	{
		return fail(component::board, error_code::already_initialized);
	}

	attempted_ = true;

	for (const auto &name : insertion_order_)
	{
		auto init_status = std::visit(
		    [](auto &device)
		    {
			    return device.init();
		    },
		    components_.at(name));

		if (!init_status)
		{
			return init_status;
		}
	}

	return {};
}
