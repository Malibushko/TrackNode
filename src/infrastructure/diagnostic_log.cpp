#include "infrastructure/diagnostic_log.h"
#include "infrastructure/supervisor.h"
#include <cstdio>

bool diagnostic_changed(const std::optional<service_diagnostic> &previous, const service_diagnostic &current)
{
	if (!previous)
	{
		return current.health == service_health::starting || current.health == service_health::degraded ||
			current.health == service_health::failed;
	}
	if (previous->health != current.health || previous->failure.has_value() != current.failure.has_value())
	{
		return true;
	}
	if (!current.failure)
	{
		return false;
	}
	return previous->failure->component != current.failure->component || previous->failure->code != current.failure->code ||
		previous->failure->hal_detail != current.failure->hal_detail;
}

//////////////////////////////////////////////////////////////////////////

status format_diagnostic(const diagnostic_record &record, std::span<char> output)
{
	const auto &diagnostic = record.diagnostic;
	int length;
	if (diagnostic.failure)
	{
		length = std::snprintf(output.data(), output.size(), "HEALTH %s %s component=%u code=%u detail=%lu",
			service_name(record.service), health_name(diagnostic.health), static_cast<unsigned>(diagnostic.failure->component),
			static_cast<unsigned>(diagnostic.failure->code), static_cast<unsigned long>(diagnostic.failure->hal_detail));
	}
	else
	{
		length = std::snprintf(output.data(), output.size(), "HEALTH %s %s", service_name(record.service), health_name(diagnostic.health));
	}
	if (length < 0 || static_cast<std::size_t>(length) >= output.size())
	{
		return fail(component::service, error_code::invalid_argument);
	}
	return {};
}

//////////////////////////////////////////////////////////////////////////

void diagnostic_log::reset()
{
	previous_.fill(std::nullopt);
	cursor_ = 0;
}

//////////////////////////////////////////////////////////////////////////

std::optional<diagnostic_record> diagnostic_log::next(const supervisor &monitor)
{
	for (std::size_t checked = 0; checked < previous_.size(); ++checked)
	{
		const auto index = cursor_;
		cursor_ = (cursor_ + 1) % previous_.size();
		const auto service = static_cast<service_id>(index);
		const auto current = monitor.diagnostic(service);
		if (diagnostic_changed(previous_[index], current))
		{
			return diagnostic_record{
				.service = service,
				.diagnostic = current
			};
		}
		previous_[index] = current;
	}
	return std::nullopt;
}

//////////////////////////////////////////////////////////////////////////

void diagnostic_log::accept(const diagnostic_record &record)
{
	previous_[static_cast<std::size_t>(record.service)] = record.diagnostic;
}
