#pragma once
#include "core/result.h"
#include <memory>

class component_ref
{
public:
	template <typename device_t>
	explicit component_ref(device_t &device)
	    : object_(std::addressof(device)), init_(
	                                           [](void *object) -> status
	                                           {
		                                           return static_cast<device_t *>(object)->init();
	                                           })
	{
	}

	status init() const
	{
		return init_(object_);
	}

private:
	void *object_;
	status (*init_)(void *);
};
