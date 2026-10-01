#include "infrastructure/radio_service.h"

#include "libs/freertos/freertos_helpers.h"

#include <type_traits>

static_assert(std::is_trivially_copyable_v<sensor_sample>);
static_assert(std::is_trivially_copyable_v<gnss_sample>);

//////////////////////////////////////////////////////////////////////////

status radio_service::start()
{
	if (inbox_)
		return fail(component::rtos, error_code::already_initialized);
	if (auto initialized = radio_.init(); !initialized)
		return initialized;

	inbox_ = xQueueCreate(4, sizeof(radio_job));

	if (!inbox_)
		return fail(component::rtos, error_code::out_of_memory);

	auto created = task_create(task_entry, "radio", configMINIMAL_STACK_SIZE * 4, this, tskIDLE_PRIORITY + 1);
	if (!created)
	{
		vQueueDelete(inbox_);
		inbox_ = nullptr;
		return tl::unexpected(created.error());
	}

	task_ = *created;
	monitor_.set_health(service_id::radio, service_health::ready);
	return {};
}

//////////////////////////////////////////////////////////////////////////

void radio_service::stop()
{
	if (task_)
	{
		vTaskDelete(task_);
		task_ = nullptr;
	}

	if (inbox_)
	{
		vQueueDelete(inbox_);
		inbox_ = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////

status radio_service::submit(const sensor_sample &sample, const std::optional<gnss_sample> &gnss, TickType_t recorded_at)
{
	if (!sample.valid)
		return fail(component::service, error_code::invalid_argument);

	if (!inbox_)
		return fail(component::service, error_code::not_initialized);

	const radio_job job{sample, gnss, recorded_at};
	if (xQueueSend(inbox_, &job, 0) != pdPASS)
	{
		dropped_.fetch_add(1, std::memory_order_relaxed);
		return fail(component::service, error_code::queue_full);
	}

	return {};
}

//////////////////////////////////////////////////////////////////////////

void radio_service::task_entry(void *argument)
{
	static_cast<radio_service *>(argument)->run();
}

//////////////////////////////////////////////////////////////////////////

void radio_service::run()
{
	constexpr TickType_t minimum_tx_interval = pdMS_TO_TICKS(1000);
	TickType_t last_tx_started = 0;
	bool has_started_tx = false;

	for (;;)
	{
		radio_job job{};

		if (xQueueReceive(inbox_, &job, portMAX_DELAY) != pdPASS)
			continue;

		const auto packet = format_radio_packet(job.sample, job.gnss, job.recorded_at);
		status sent{};
		if (!packet)
		{
			sent = tl::unexpected(packet.error());
		}
		else
		{
			if (power_mode_.load(std::memory_order_relaxed) == power_mode::low && has_started_tx)
			{
				const TickType_t elapsed = xTaskGetTickCount() - last_tx_started;
				if (elapsed < minimum_tx_interval)
					vTaskDelay(minimum_tx_interval - elapsed);
			}
			last_tx_started = xTaskGetTickCount();
			has_started_tx = true;
			sent = radio_.transmit(std::span<const std::uint8_t>{packet->bytes}.first(packet->length), 5000);
		}

		if (!sent)
		{
			dropped_.fetch_add(1, std::memory_order_relaxed);
			monitor_.report(service_id::radio, sent.error(), service_health::degraded);
			(void)logger_.write("Radio sample failed: %u\r\n", static_cast<unsigned>(sent.error().code));
		}
		else
		{
			monitor_.set_health(service_id::radio, service_health::ready);
		}
		monitor_.progress(service_id::radio, xTaskGetTickCount());
	}
}

//////////////////////////////////////////////////////////////////////////
