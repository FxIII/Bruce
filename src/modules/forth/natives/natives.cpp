#include "natives_internal.h"
#include "../uforth.h"
#include <Arduino.h>

#define MAX_NATIVES 128

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

static bool load_module(const NativeModule *mod) {
    CELL idx = find_word((char*)mod->check_word, (uint8_t)strlen(mod->check_word), NULL, NULL, NULL);
    if (idx != 0) {
        log_d("Module '%s' already loaded", mod->name);
        return false;
    }

    if (mod->bindings) {
        mod->bindings();
    }
    if (mod->definitions) {
        mod->definitions();
    }
    log_i("Module '%s' loaded successfully", mod->name);
    return true;
}

static void fn_use(void) {
    char *w = uforth_next_word();
    int len = uforth_iram->tibwordlen;
    if (len <= 0) {
        forth_output("Usage: use <module>\n");
        return;
    }

    char name[32];
    if (len >= (int)sizeof(name)) len = sizeof(name) - 1;
    memcpy(name, w, len);
    name[len] = '\0';

    size_t count = 0;
    const NativeModule *modules = get_native_modules(count);

    // Special dependency check: lora and gps depend on reactor for attach-service
    if (strcmp(name, "lora") == 0 || strcmp(name, "gps") == 0) {
        for (size_t i = 0; i < count; i++) {
            if (strcmp(modules[i].name, "reactor") == 0) {
                load_module(&modules[i]);
                break;
            }
        }
    }

    for (size_t i = 0; i < count; i++) {
        if (strcmp(modules[i].name, name) == 0) {
            load_module(&modules[i]);
            return;
        }
    }

    char err_msg[64];
    snprintf(err_msg, sizeof(err_msg), "Unknown module '%s'\n", name);
    forth_output(err_msg);
}

// Phase 1: Aggregates internal C++ binding registrations before core load
void forth_natives_register_bindings() {
    size_t count = 0;
    const NativeModule *modules = get_native_modules(count);
    for (size_t i = 0; i < count; i++) {
        if (modules[i].bootstrap && modules[i].bindings) {
            modules[i].bindings();
        }
    }
    forth_register("use", fn_use);
}

// Phase 2: Aggregates internal Forth word definition registrations after core load
void forth_natives_register_definitions() {
    size_t count = 0;
    const NativeModule *modules = get_native_modules(count);
    for (size_t i = 0; i < count; i++) {
        if (modules[i].bootstrap && modules[i].definitions) {
            modules[i].definitions();
        }
    }
}
