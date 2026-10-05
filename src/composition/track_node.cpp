#include "composition/track_node.h"

#include "common/common.h"

status track_node::init()
{
	if (init_requested_)
	{
		return fail(component::board, error_code::already_initialized);
	}
	auto created = task_create(init_task, "init", configMINIMAL_STACK_SIZE * 6, this, configMAX_PRIORITIES - 1);
	if (!created)
	{
		return tl::make_unexpected(created.error());
	}
	init_requested_ = true;
	return {};
}

//////////////////////////////////////////////////////////////////////////

void track_node::init_task(void *argument)
{
	auto &application = *static_cast<track_node *>(argument);

	const auto started = application.startup();

	if (!started)
	{
		application.stop_workers();
		if (!application.can_signal_error())
		{
			panic();
		}
		for (;;)
		{
			application.indicator_.update_once(common::get_tick());
			if (!application.can_signal_error())
			{
				panic();
			}
			vTaskDelay(pdMS_TO_TICKS(50));
		}
	}

	vTaskDelete(nullptr);
}

//////////////////////////////////////////////////////////////////////////

status track_node::startup()
{
	if (attempted_)
	{
		return fail(component::board, error_code::already_initialized);
	}

	attempted_ = true;

	auto started = services_.prepare();
	if (started)
	{
		supervisor_.configure(services_.mask());
		started = indicator_.init_diagnostic();
	}
	if (started)
	{
		started = devices_.init();
	}
	if (!started)
	{
		supervisor_.report_startup(started.error());
		return started;
	}

	started = services_.start();
	if (!started)
	{
		supervisor_.report_startup(started.error());
	}
	return started;
}

//////////////////////////////////////////////////////////////////////////

void track_node::stop_workers()
{
	services_.stop();
}

//////////////////////////////////////////////////////////////////////////

void track_node::launch()
{
	vTaskStartScheduler();
	for (;;)
	{
		// Scheduler return means a fatal startup failure.
	}
}

//////////////////////////////////////////////////////////////////////////
