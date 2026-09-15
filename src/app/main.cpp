#include "app/track_node.h"
#include "board/track_node_board.h"
#include "common/common.h"
#include "platform/error.h"

int main()
{
	track_node_board hardware;
	track_node application{ hardware };
	const auto init_status = application.init();

	if (!init_status && !application.can_signal_error())
	{
		panic();
	}

	while (true)
	{
		application.update(common::get_tick());
	}
}
