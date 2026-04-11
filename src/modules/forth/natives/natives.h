#pragma once

#include "../uforth.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*forth_native_fn)(void);

void forth_natives_reset(void);
uint16_t forth_register(const char *name, forth_native_fn fn);
uforth_stat forth_dispatch(CELL id);
void forth_register_all(void);

#ifdef __cplusplus
}
#endif
