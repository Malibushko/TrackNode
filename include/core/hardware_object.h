#pragma once

class hardware_object
{

protected:
	hardware_object() = default;
	~hardware_object() = default;

public:
	hardware_object(const hardware_object &) = delete;
	hardware_object &operator=(const hardware_object &) = delete;

	hardware_object(hardware_object &&) = default;
	hardware_object& operator=(hardware_object &&) = default;
};
