#pragma once

#include "natives.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*forth_native_fn)(void);
typedef void (*module_fn)(void);

struct NativeModule {
    const char *name;
    const char *check_word;
    module_fn bindings;
    module_fn definitions;
    bool bootstrap;
};

uint16_t forth_register(const char *name, forth_native_fn fn);
uforth_stat forth_dispatch(CELL id);

// Internal native module contract functions
void core_bindings(void);
void core_definitions(void);

void display_bindings(void);
void display_definitions(void);

void block_bindings(void);
void block_definitions(void);

void reactor_bindings(void);
void reactor_definitions(void);
void forth_reactor_step(void);

void lora_bindings(void);
void lora_definitions(void);

void gps_bindings(void);
void gps_definitions(void);

void sys_bindings(void);

#ifdef __cplusplus
}

inline const NativeModule* get_native_modules(size_t &count) {
    static const NativeModule modules[] = {
        { "core",     "dup",               core_bindings,     core_definitions,    true  },
        { "sys",      "sys",               sys_bindings,      nullptr,             false },
        { "display",  "cls",               display_bindings,  nullptr,             false },
        { "block",    "block-read",        block_bindings,    nullptr,             true  },
        { "reactor",  ".services",         reactor_bindings,  reactor_definitions, false },
        { "lora",     "lora-init",         lora_bindings,     lora_definitions,    false },
        { "gps",      "gps-init",          gps_bindings,      gps_definitions,     false },
    };
    count = sizeof(modules) / sizeof(modules[0]);
    return modules;
}
#endif
