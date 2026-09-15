#include "board/board.h"

status board::init(std::span<const component_ref> components)
{
	if (attempted_)
	{
		return fail(component::board, error_code::already_initialized);
	}
	attempted_ = true;
	for (const auto &device : components)
	{
		auto init_status = device.init();
		if (!init_status)
		{
			return init_status;
		}
	}
	return {};
}
