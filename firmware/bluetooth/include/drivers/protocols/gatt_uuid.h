#pragma once

#include <zephyr/bluetooth/uuid.h>

namespace ble::gatt_uuid
{
inline const bt_uuid_16 environmental_sensing = BT_UUID_INIT_16(0x181a);
inline const bt_uuid_16 temperature = BT_UUID_INIT_16(0x2a6e);
inline const bt_uuid_16 pressure = BT_UUID_INIT_16(0x2a6d);
inline const bt_uuid_128 gnss = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xe0910004, 0x7e83, 0x4c9c, 0xa89e, 0xbfe03c940001)
);
inline const bt_uuid_128 measurement_status = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xe0910005, 0x7e83, 0x4c9c, 0xa89e, 0xbfe03c940001)
);
inline const bt_uuid_128 service = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xe0910001, 0x7e83, 0x4c9c, 0xa89e, 0xbfe03c940001)
);

inline const bt_uuid_128 rx = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xe0910002, 0x7e83, 0x4c9c, 0xa89e, 0xbfe03c940001)
);

inline const bt_uuid_128 tx = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xe0910003, 0x7e83, 0x4c9c, 0xa89e, 0xbfe03c940001)
);
}
