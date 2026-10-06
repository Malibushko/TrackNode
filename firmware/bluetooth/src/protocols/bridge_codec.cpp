#include "protocols/bridge_codec.h"

namespace ble
{
result<std::size_t> bridge_codec::encode(const ble_bridge::frame &, mutable_data_view) const
{
	return fail(component::protocol, error_code::not_implemented);
}

result<ble_bridge::frame> bridge_codec::decode(data_view) const
{
	return fail(component::protocol, error_code::not_implemented);
}
}
