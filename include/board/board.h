#pragma once
#include "core/component_ref.h"
#include <span>

class board
{
public:
	status init(std::span<const component_ref> components);

private:
	bool attempted_ = false;
};
