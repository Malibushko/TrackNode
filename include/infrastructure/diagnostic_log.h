#pragma once

#include "infrastructure/service_status.h"
#include <span>

class supervisor;

struct diagnostic_record
{
	service_id service;
	service_diagnostic diagnostic;
};

bool diagnostic_changed(const std::optional<service_diagnostic> &previous, const service_diagnostic &current);
status format_diagnostic(const diagnostic_record &record, std::span<char> output);

class diagnostic_log
{
public:
	void reset();
	std::optional<diagnostic_record> next(const supervisor &monitor);
	void accept(const diagnostic_record &record);

private:
	std::array<std::optional<service_diagnostic>, static_cast<std::size_t>(service_id::count)> previous_{};
	std::size_t cursor_ = 0;
};
