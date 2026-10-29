#pragma once
struct device;
enum pm_device_action
{
	PM_DEVICE_ACTION_SUSPEND,
	PM_DEVICE_ACTION_RESUME
};
inline int fake_pm_suspends = 0;
inline int fake_pm_resumes = 0;
inline int pm_device_action_run(const device *, pm_device_action action)
{
	if (action == PM_DEVICE_ACTION_SUSPEND)
	{
		++fake_pm_suspends;
	}
	else
	{
		++fake_pm_resumes;
	}
	return 0;
}
