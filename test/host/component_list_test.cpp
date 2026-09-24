#include "core/component_list.h"

#include <cassert>

struct device
{
    int &calls;
    bool fails = false;
    status init()
    {
        ++calls;
        if (fails) return fail(component::gpio, error_code::invalid_config);
        return {};
    }
};

struct other_device
{
    status init() { return {}; }
};

int main()
{
    int first_calls = 0, failed_calls = 0, last_calls = 0, deferred_calls = 0;
    device first{first_calls};
    device failed{failed_calls, true};
    device last{last_calls};
    device deferred{deferred_calls};

    component_list components;
    assert(components.add("first", first, component_startup::initialize));
    assert(components.add("deferred", deferred, component_startup::defer));
    assert(components.add("failed", failed, component_startup::initialize));
    assert(components.add("last", last, component_startup::initialize));
    assert(!components.add("first", last, component_startup::initialize));
    assert(!components.add("alias", first, component_startup::initialize));
    assert(components.get<device>("first") == &first);
    assert(components.get<device>("missing") == nullptr);
    assert(components.get<other_device>("first") == nullptr);

    auto outcome = components.init();
    assert(!outcome && outcome.error().component == component::gpio);
    assert(first_calls == 1 && failed_calls == 1 && last_calls == 0);
    assert(deferred_calls == 0);
    assert(!components.init());
    assert(!components.add("late", last, component_startup::defer));

    component_list successful;
    assert(successful.add("first", first, component_startup::initialize));
    assert(successful.add("deferred", deferred, component_startup::defer));
    assert(successful.add("last", last, component_startup::initialize));
    assert(successful.init());
    assert(first_calls == 2 && last_calls == 1 && deferred_calls == 0);
}
