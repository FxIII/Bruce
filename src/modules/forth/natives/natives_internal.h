#pragma once

#include "natives.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*forth_native_fn)(void);

uint16_t forth_register(const char *name, forth_native_fn fn);
uforth_stat forth_dispatch(CELL id);

// Internal native module contract functions
void core_bindings(void);
void core_definitions(void);

void display_bindings(void);

void block_bindings(void);

void reactor_bindings(void);
void reactor_definitions(void);
void forth_reactor_step(void);

void lora_bindings(void);
void lora_definitions(void);

void gps_bindings(void);
void gps_definitions(void);

#ifdef __cplusplus
}
#endif
