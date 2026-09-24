#include "composition/track_node.h"

#include "common/common.h"

track_node::track_node(component_list &devices, uart &console, bmp280 &sensor,
                       sd_card &storage, watchdog &watchdog_device)
    : devices_(devices),
      supervisor_(watchdog_device),
      logger_(console, supervisor_),
      storage_(storage, storage_service::file_config{ "logs", "logs/data.csv",
                                                     sample_csv_header }, logger_, supervisor_),
      sensor_(sensor),
      indicator_(logger_, supervisor_),
      controller_(sensor_, storage_, indicator_, logger_, supervisor_),
      console_(console, controller_, logger_, supervisor_)
{
}

//////////////////////////////////////////////////////////////////////////

status track_node::init()
{
	auto created = task_create(init_task, "init", configMINIMAL_STACK_SIZE * 4, this, configMAX_PRIORITIES - 1);
	if (!created)
	{
		return tl::make_unexpected(created.error());
	}
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

	auto started = indicator_.init_diagnostic();
	if (started)
	{
		started = devices_.init();
	}
	if (!started)
	{
		supervisor_.report_startup(started.error());
		return started;
	}

	started = logger_.start()
	              .and_then([&]() -> status { return storage_.start(); })
	              .and_then([&]() -> status { return indicator_.start(); })
	              .and_then([&]() -> status { return controller_.start(); })
	              .and_then([&]() -> status { return console_.start(); })
	              .and_then([&]() -> status { return supervisor_.start(); });
	if (!started)
	{
		supervisor_.report_startup(started.error());
	}
	return started;
}

//////////////////////////////////////////////////////////////////////////

void track_node::stop_workers()
{
	supervisor_.stop();
	console_.stop();
	controller_.stop();
	indicator_.stop();
	storage_.stop();
	logger_.stop();
}

//////////////////////////////////////////////////////////////////////////

track_node::delivery_statistics track_node::statistics() const
{
	return { console_.rejected_commands(), logger_.dropped_count(),
	         logger_.failed_writes(), storage_.snapshot().dropped_samples };
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
