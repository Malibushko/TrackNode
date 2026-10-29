#pragma once
#include <cstdint>
#include <cstddef>
#include <cerrno>
#ifndef EHOSTDOWN
#define EHOSTDOWN 112
#endif
struct bt_data
{
	std::uint8_t type;
	std::uint8_t data_len;
	const std::uint8_t *data;
};
#define BT_DATA(type, data, len) bt_data{type, len, reinterpret_cast<const std::uint8_t *>(data)}
inline constexpr std::uint8_t BT_LE_AD_GENERAL = 2;
inline constexpr std::uint8_t BT_LE_AD_NO_BREDR = 4;
inline constexpr std::uint8_t BT_DATA_FLAGS = 1;
inline constexpr std::uint8_t BT_DATA_NAME_COMPLETE = 9;
struct bt_le_adv_param
{
	std::uint8_t id;
	std::uint8_t sid;
	std::uint8_t secondary_max_skip;
	std::uint32_t options;
	std::uint32_t interval_min;
	std::uint32_t interval_max;
	const void *peer;
};
inline constexpr std::uint32_t BT_LE_ADV_OPT_CONN = 3;
inline constexpr std::uint32_t BT_GAP_ADV_FAST_INT_MIN_1 = 0x0030;
inline constexpr std::uint32_t BT_GAP_ADV_FAST_INT_MAX_1 = 0x0060;
inline constexpr std::uint32_t BT_GAP_ADV_SLOW_INT_MIN = 0x0640;
inline constexpr std::uint32_t BT_GAP_ADV_SLOW_INT_MAX = 0x0780;
#define BT_LE_ADV_PARAM_INIT(_options, _int_min, _int_max, _peer) \
	{ .id = 0, .sid = 0, .secondary_max_skip = 0, .options = (_options), .interval_min = (_int_min), .interval_max = (_int_max), .peer = (_peer) }
inline unsigned advertising_starts = 0;
inline std::uint32_t advertising_interval_min = 0;
inline int enable_error = 0;
inline int name_error = 0;
inline int advertising_error = 0;
inline bool stack_enabled = false;
inline int bt_enable(void *)
{
	if (enable_error != 0)
	{
		return enable_error;
	}
	if (stack_enabled)
	{
		return -EALREADY;
	}
	stack_enabled = true;
	return 0;
}
inline int bt_set_name(const char *)
{
	return name_error;
}
inline int bt_le_adv_start(const bt_le_adv_param *param, const bt_data *, std::size_t, const bt_data *, std::size_t)
{
	if (advertising_error == 0)
	{
		++advertising_starts;
		advertising_interval_min = param->interval_min;
	}
	return advertising_error;
}
inline int bt_le_adv_stop()
{
	return advertising_error;
}
