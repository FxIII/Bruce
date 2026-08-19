#include "natives_internal.h"
#include <Arduino.h>

#define MAX_NATIVES 64

static forth_output_fn _output_fn = nullptr;
void forth_set_output(forth_output_fn fn) { _output_fn = fn; }
void forth_output(const char *s) { if (_output_fn) _output_fn(s); }

static forth_cls_fn _cls_fn = nullptr;
void forth_set_cls(forth_cls_fn fn) { _cls_fn = fn; }
void forth_cls(void) { if (_cls_fn) _cls_fn(); }

static forth_native_fn _fns[MAX_NATIVES + 1]; // 1-based
static uint16_t _count = 0;
static bool _restore_mode = false;

void forth_set_restore_mode(bool restore) {
    _restore_mode = restore;
}

void forth_natives_reset() {
    _count = 0;
    for (int i = 0; i <= MAX_NATIVES; i++) _fns[i] = nullptr;
}

uint16_t forth_register(const char *name, forth_native_fn fn) {
    if (_count >= MAX_NATIVES) { log_e("forth_register: overflow, cannot register '%s'", name); return 0; }
    uint16_t id = ++_count;
    _fns[id] = fn;
    if (!_restore_mode) {
        forth_define_native(name, id);
    }
    log_d("forth_register: '%s' -> id=%d (restore=%d)", name, id, _restore_mode);
    return id;
}

uforth_stat forth_dispatch(CELL id) {
    if (id < 1 || id > _count || _fns[id] == nullptr) {
        log_e("forth_dispatch: invalid id=%d", id);
        return E_NOT_A_WORD;
    }
    _fns[id]();
    return UFORTH_OK;
}

// Phase 1: Aggregates internal C++ binding registrations before core load
void forth_natives_register_bindings() {
    core_bindings();
    display_bindings();
    block_bindings();
}

// Phase 2: Aggregates internal Forth word definition registrations after core load
void forth_natives_register_definitions() {
    core_definitions();
}
