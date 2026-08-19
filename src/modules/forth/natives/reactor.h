#pragma once

#include "../uforth.h"

#ifdef __cplusplus
extern "C" {
#endif

void reactor_bindings(void);
void reactor_definitions(void);
void forth_reactor_step(void);

#ifdef __cplusplus
}
#endif
