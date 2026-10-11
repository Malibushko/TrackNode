#include "infrastructure/control_button_service.h"
#include "infrastructure/bluetooth_service.h"
#include <algorithm>

status control_button_service::start()
{
	if (task_)
	{
		return fail(component::service, error_code::already_initialized);
	}
	if (const auto validated = validate_config(); !validated)
	{
		return validated;
	}
	if (const auto initialized = input_.init(); !initialized)
	{
		return initialized;
	}
	const auto created = task_create(task_entry, "control_button", configMINIMAL_STACK_SIZE * 3,
		this, tskIDLE_PRIORITY + 1);
	if (!created)
	{
		return tl::unexpected(created.error());
	}
	task_ = *created;
	monitor_.set_health(service_id::control_button, service_health::ready);
	return {};
}

//////////////////////////////////////////////////////////////////////////

void control_button_service::stop()
{
	if (task_)
	{
		vTaskDelete(task_);
		task_ = nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////

status control_button_service::validate_config() const
{
	const auto invalid_button_timing = pdMS_TO_TICKS(config_.poll_ms) == 0 || config_.debounce_ms == 0 ||
		config_.pairing_hold_ms <= config_.debounce_ms;
	const auto button_not_configured_as_input = config_.input.mode != GPIO_MODE_INPUT;
	const auto hold_thresholds_missing_or_too_short = config_.hold_thresholds_ms.empty() ||
		config_.hold_thresholds_ms.front() <= config_.debounce_ms;
	const auto hold_thresholds_not_strictly_increasing = std::adjacent_find(
		config_.hold_thresholds_ms.begin(), config_.hold_thresholds_ms.end(),
		[](auto left, auto right){ return left >= right; }) != config_.hold_thresholds_ms.end();
	const auto pairing_threshold_not_registered = std::find(config_.hold_thresholds_ms.begin(),
		config_.hold_thresholds_ms.end(), config_.pairing_hold_ms) == config_.hold_thresholds_ms.end();
	if (invalid_button_timing || button_not_configured_as_input || hold_thresholds_missing_or_too_short ||
		hold_thresholds_not_strictly_increasing || pairing_threshold_not_registered)
	{
		return fail(component::service, error_code::invalid_config);
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////


void control_button_service::task_entry(void *argument)
{
	static_cast<control_button_service *>(argument)->run();
}

//////////////////////////////////////////////////////////////////////////

void control_button_service::run()
{
	for (;;)
	{
		const auto now = xTaskGetTickCount();
		const auto input = input_.read();
		status outcome;
		if (!input)
		{
			outcome = tl::unexpected(input.error());
		}
		else if (const auto gesture = gesture_recognizer_.update(*input == gpio::state::set, pdTICKS_TO_MS(now)))
		{
			outcome = handle_gesture(*gesture);
		}
		if (!outcome)
		{
			monitor_.report(service_id::control_button, outcome.error(), service_health::degraded);
		}
		monitor_.progress(service_id::control_button, now);
		vTaskDelay(pdMS_TO_TICKS(config_.poll_ms));
	}
}

//////////////////////////////////////////////////////////////////////////


status control_button_service::handle_gesture(const control_button_gesture &gesture)
{
	switch (gesture.type)
	{
		case control_button_gesture::kind::hold_threshold_reached:
			if (gesture.duration_ms == config_.pairing_hold_ms)
			{
				return handle_pairing_mode_gesture();
			}
			return {};
		case control_button_gesture::kind::released:
			return {};
	}
	return fail(component::service, error_code::invalid_argument);
}

//////////////////////////////////////////////////////////////////////////

status control_button_service::handle_pairing_mode_gesture()
{
	return bluetooth_service_.on_pairing_mode_requested();
}
