#include "natives.h"
#include "audio.h"
#include <Arduino.h>

#define MAX_NATIVES 64

static forth_native_fn _fns[MAX_NATIVES + 1]; // 1-based
static uint16_t _count = 0;

void forth_natives_reset() {
    _count = 0;
    for (int i = 0; i <= MAX_NATIVES; i++) _fns[i] = nullptr;
}

uint16_t forth_register(const char *name, forth_native_fn fn) {
    if (_count >= MAX_NATIVES) { log_e("forth_register: overflow, cannot register '%s'", name); return 0; }
    uint16_t id = ++_count;
    _fns[id] = fn;
    forth_define_native(name, id);
    log_d("forth_register: '%s' -> id=%d", name, id);
    return id;
}

uforth_stat forth_dispatch(CELL id) {
    log_d("forth_dispatch: id=%d _count=%d", id, _count);
    if (id < 1 || id > _count || _fns[id] == nullptr) {
        log_e("forth_dispatch: invalid id=%d", id);
        return E_NOT_A_WORD;
    }
    _fns[id]();
    return UFORTH_OK;
}

void forth_register_all() {
    forth_register_audio();
}
