#include "infrastructure/bluetooth_service.h"

status bluetooth_service::start()
{
	// TODO(user): bounded queues, worker task, backend init and health reporting.
	return fail(component::service, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

void bluetooth_service::stop()
{
	// TODO(user): stop worker before destroying queues; handle partial startup.
	backend_.stop();
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::submit(const telemetry_sample &)
{
	// TODO(user): copy sample into a bounded queue, then encode/fragment in worker.
	return fail(component::service, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

status bluetooth_service::submit_reply(const command_reply &)
{
	// TODO(user): separate reply queue, preserve request/session, no blocking here.
	return fail(component::service, error_code::not_implemented);
}

//////////////////////////////////////////////////////////////////////////

void bluetooth_service::set_power_mode(power_mode)
{
	// TODO(user): decide advertising/connection policy; no fake readiness transition.
}

//////////////////////////////////////////////////////////////////////////
