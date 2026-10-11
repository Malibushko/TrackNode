#pragma once

#include "drivers/gpio.h"
#include "infrastructure/control_button_gesture.h"
#include "infrastructure/supervisor.h"
#include "libs/freertos/freertos_helpers.h"

class bluetooth_service;

struct control_button_config
{
	gpio_config input{};
	std::uint32_t poll_ms = 10;
	std::uint32_t debounce_ms = 30;
	std::span<const std::uint32_t> hold_thresholds_ms;
	std::uint32_t pairing_hold_ms = 5000;
};

class control_button_service
{
public:
	control_button_service(control_button_config config, bluetooth_service &bluetooth, supervisor &monitor)
		: config_(config), input_(config.input), bluetooth_service_(bluetooth), monitor_(monitor),
		  gesture_recognizer_(config.debounce_ms, config.hold_thresholds_ms) {}

	status start();
	void stop();

private:
	status validate_config() const;

	static void task_entry(void *argument);
	void run();

	status handle_gesture(const control_button_gesture &gesture);
	status handle_pairing_mode_gesture();

private:
	control_button_config config_;
	gpio input_;
	bluetooth_service &bluetooth_service_;
	supervisor &monitor_;

	control_button_gesture_recognizer gesture_recognizer_;

	TaskHandle_t task_ = nullptr;
};
