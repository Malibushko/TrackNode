#include "protocols/bridge_codec.h"

namespace ble
{
result<std::size_t> bridge_codec::encode(const bridge_frame &, mutable_data_view) const
{
	return fail(component::protocol, error_code::not_implemented);
}

result<bridge_frame> bridge_codec::decode(data_view) const
{
	return fail(component::protocol, error_code::not_implemented);
}
}
