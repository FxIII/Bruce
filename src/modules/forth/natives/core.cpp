#include "natives_internal.h"
#include "../uforth.h"
#include "core/mykeyboard.h"
#include <Arduino.h>

extern "C" void make_immediate(void);

static void fn_key() {
    while (true) {
        keyStroke ks = _getKeyPress();
        if (ks.pressed) {
            if (ks.del) {
                dpush(8);
                return;
            }
            if (ks.enter) {
                dpush(13);
                return;
            }
            if (!ks.word.empty()) {
                dpush((uint8_t)ks.word[0]);
                return;
            }
            if (ks.exit_key) {
                dpush(27);
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void fn_emit() {
    char c = (char)dpop();
    char s[2] = {c, '\0'};
    forth_output(s);
}

static void fn_cr() {
    forth_output("\n");
}

static void fn_dot() {
    char buf[24];
    DCELL n = dpop();
    snprintf(buf, sizeof(buf), "%lld ", (long long)n);
    Serial.printf("[DEBUG fn_dot] outputting: '%s'\n", buf);
    forth_output(buf);
}

static void fn_words() {
    Serial.println("[DEBUG fn_words] called");
    forth_output("\n");
    CELL idx = dict->last_word_idx;
    int count = 0;
    while (idx) {
        uint8_t flags = (uint8_t)uforth_dict[idx + 1];
        uint8_t len   = flags & 0x3F;
        if (len > 0 && len < 63) {
            char name[64];
            memcpy(name, (char *)(uforth_dict + idx + 2), len);
            name[len] = '\0';
            Serial.printf("[DEBUG fn_words] word: '%s'\n", name);
            forth_output(name);
            forth_output(" ");
            count++;
        }
        idx = uforth_dict[idx];
    }
    Serial.printf("[DEBUG fn_words] total words: %d\n", count);
    forth_output("\n");
}

static void fn_line_comment() {
    uforth_iram->tibidx = uforth_iram->tibclen;
}

static void fn_paren_comment() {
    while (uforth_iram->tibidx < uforth_iram->tibclen) {
        char c = uforth_iram->tib[uforth_iram->tibidx++];
        if (c == ')') break;
    }
}

static void fn_ms() {
    uint32_t ms = (uint32_t)dpop();
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

static void fn_us() {
    uint32_t us = (uint32_t)dpop();
    if (us > 0) {
        delayMicroseconds(us);
    }
}

static void fn_key_query() {
    keyStroke ks = _getKeyPress();
    if (ks.pressed) {
        if (ks.del) { dpush(8); return; }
        if (ks.enter) { dpush(13); return; }
        if (!ks.word.empty()) { dpush((uint8_t)ks.word[0]); return; }
        if (ks.exit_key) { dpush(27); return; }
    }
    dpush(0);
}

void core_bindings() {
    forth_register("emit", fn_emit);
    forth_register("cr", fn_cr);
    forth_register(".", fn_dot);
    forth_register("words", fn_words);
    forth_register("\\", fn_line_comment); make_immediate();
    forth_register("(", fn_paren_comment); make_immediate();
    forth_register("key", fn_key);
    forth_register("key?", fn_key_query);
    forth_register("ms", fn_ms);
    forth_register("us", fn_us);
}

void core_definitions() {
    uforth_interpret(": count dup 1+ swap c@ ;");
}
