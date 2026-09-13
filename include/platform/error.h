#pragma once
#include "core/result.h"

enum class program_state
{
	ok,
	initialization_error,
	runtime_error,
	hal_error
};

inline program_state g_state = program_state::ok;

void error_handler(error_code error_code);

[[noreturn]] void panic();