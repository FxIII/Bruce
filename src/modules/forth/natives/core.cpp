#include "core.h"
#include "natives.h"
#include "../uforth.h"
#include <Arduino.h>

static void _fn_emit() {
    char c = (char)dpop();
    char s[2] = {c, '\0'};
    forth_output(s);
}

static void _fn_cr() {
    forth_output("\n");
}

static void _fn_dot() {
    char buf[24];
    DCELL n = dpop();
    snprintf(buf, sizeof(buf), "%lld ", (long long)n);
    forth_output(buf);
}

static void _fn_cls() {
    forth_cls();
}

static void _fn_words() {
    forth_output("\n");
    CELL idx = dict->last_word_idx;
    while (idx) {
        uint8_t flags = (uint8_t)uforth_dict[idx + 1];
        uint8_t len   = flags & 0x3F;
        if (len > 0 && len < 63) {
            char name[64];
            memcpy(name, (char *)(uforth_dict + idx + 2), len);
            name[len] = '\0';
            forth_output(name);
            forth_output(" ");
        }
        idx = uforth_dict[idx];
    }
    forth_output("\n");
}

void forth_register_core() {
    forth_register("emit",  _fn_emit);
    forth_register("cr",    _fn_cr);
    forth_register(".",     _fn_dot);
    forth_register("cls",   _fn_cls);
    forth_register("words", _fn_words);
}
