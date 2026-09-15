#include "board/board.h"
#include <array>
#include <cassert>
struct device
{
    int& calls;
    bool fails;
    status init()
    {
        ++calls;
        if (fails) return fail(component::gpio, error_code::invalid_config);
        return {};
    }
};
int main()
{
    int first_calls=0, failed_calls=0, last_calls=0;
    device first{first_calls,false}, failed{failed_calls,true}, last{last_calls,false};
    const std::array components{component_ref{first},component_ref{failed},component_ref{last}};
    board lifecycle;
    auto outcome=lifecycle.init(components);
    assert(!outcome && outcome.error().component==component::gpio);
    assert(first_calls==1 && failed_calls==1 && last_calls==0);
    assert(!lifecycle.init(components));
    assert(first_calls==1 && failed_calls==1);
    board successful;
    const std::array valid{component_ref{first},component_ref{last}};
    assert(successful.init(valid));
    assert(first_calls==2 && last_calls==1);
}
