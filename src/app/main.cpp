#include "app/track_node.h"
#include "board/clock.h"
#include "board/track_node_board.h"
#include "platform/error.h"

static track_node_board hardware;
static track_node application{ hardware };

int main()
{
	HAL_Init();
	auto init_status = init_system_clock()
							.and_then([]{ return application.init(); });

	if (!init_status && !application.can_signal_error())
	{
		panic();
	}

	application.launch();
}
