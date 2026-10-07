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
inline constexpr int BT_LE_ADV_CONN_FAST_1 = 0;
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
inline int bt_le_adv_start(int, const bt_data *, std::size_t, const bt_data *, std::size_t)
{
	return advertising_error;
}
inline int bt_le_adv_stop()
{
	return advertising_error;
}
