#include <cstdint>

extern "C"
{

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

int main();

void default_handler();
void reset_handler();

}

using handler_t = void(*)();

using vector_entry_t = std::uintptr_t;

__attribute__((section(".isr_vector"), used))
const vector_entry_t vector_table[] = {
    reinterpret_cast<vector_entry_t>(&_estack),
    reinterpret_cast<vector_entry_t>(reset_handler),

    reinterpret_cast<vector_entry_t>(default_handler),
    reinterpret_cast<vector_entry_t>(default_handler),
    reinterpret_cast<vector_entry_t>(default_handler),
    reinterpret_cast<vector_entry_t>(default_handler),
    reinterpret_cast<vector_entry_t>(default_handler),

    0,
    0,
    0,
    0,

    reinterpret_cast<vector_entry_t>(default_handler),
    reinterpret_cast<vector_entry_t>(default_handler),

    0,

    reinterpret_cast<vector_entry_t>(default_handler),
    reinterpret_cast<vector_entry_t>(default_handler),
};

void reset_handler()
{
    // .data: Flash -> RAM
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;

    while (dst < &_edata) *dst++ = *src++;

    dst = &_sbss;

    // .bss: zero
    while (dst < &_ebss) *dst++ = 0;

    main();

    while (true) {};
}

void default_handler() {
    while (true)
    {
        // loop
    }
}