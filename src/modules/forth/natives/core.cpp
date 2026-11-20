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
    forth_output(buf);
}

static void fn_words() {
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
            forth_output(name);
            forth_output(" ");
            count++;
        }
        idx = uforth_dict[idx];
    }
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

#define OP_DLIT 2

static void fn_s_quote() {
    if (uforth_iram->tibidx < uforth_iram->tibclen && uforth_iram->tib[uforth_iram->tibidx] == ' ') {
        uforth_iram->tibidx++;
    }

    char buf[128];
    int len = 0;
    while (uforth_iram->tibidx < uforth_iram->tibclen && len < (int)sizeof(buf) - 1) {
        char c = uforth_iram->tib[uforth_iram->tibidx++];
        if (c == '"') break;
        buf[len++] = c;
    }
    buf[len] = '\0';

    if (uforth_iram->compiling == 0) {
        // Interpreter mode: rotating buffer in PAD to support multiple literals per line
        static int pad_slot = 0;
        pad_slot = (pad_slot == 0) ? 32 : 0; // Toggle between offset 1 and 33
        DCELL target_idx = PAD_ADDR + 1 + pad_slot;
        char *pad_str = (char *)&uforth_ram[target_idx];
        memcpy(pad_str, buf, len + 1);
        dpush(target_idx);
    } else {
        // Compile mode: allocate RAM for anonymous string
        int cells = (len + 1 + (int)sizeof(DCELL) - 1) / (int)sizeof(DCELL);
        DCELL ram_idx = VAR_ALLOT(cells);
        memcpy(&uforth_ram[ram_idx], buf, len + 1);

        // Compile DLIT <ram_idx>
        dict_append(OP_DLIT);
        dict_append((uint16_t)(((uint32_t)ram_idx) >> 16));
        dict_append((uint16_t)(((uint32_t)ram_idx) & 0xFFFF));
    }
}

static void fn_s_dot() {
    DCELL addr = dpop();
    const char *s = (const char *)&uforth_ram[addr];
    forth_output(s);
}

static void fn_type() {
    DCELL len = dpop();
    DCELL addr = dpop();
    if (len <= 0) return;
    const char *s = (const char *)&uforth_ram[addr];
    char buf[128];
    while (len > 0) {
        size_t chunk = (len < (DCELL)(sizeof(buf) - 1)) ? (size_t)len : (sizeof(buf) - 1);
        memcpy(buf, s, chunk);
        buf[chunk] = '\0';
        forth_output(buf);
        s += chunk;
        len -= chunk;
    }
}

static void fn_slen() {
    DCELL addr = dpop();
    const char *s = (const char *)&uforth_ram[addr];
    dpush((DCELL)strlen(s));
}

static void fn_scount() {
    DCELL addr = dpop();
    const char *s = (const char *)&uforth_ram[addr];
    dpush(addr);
    dpush((DCELL)strlen(s));
}

static void fn_seq() {
    DCELL a2 = dpop();
    DCELL a1 = dpop();
    const char *s1 = (const char *)&uforth_ram[a1];
    const char *s2 = (const char *)&uforth_ram[a2];
    dpush(strcmp(s1, s2) == 0 ? 1 : 0);
}

static void fn_dict_to_s() {
    DCELL ram_addr = dpop();
    CELL dict_addr = (CELL)dpop();
    CELL len = uforth_dict[dict_addr];
    char *dst = (char *)&uforth_ram[ram_addr];
    size_t d_idx = dict_addr + 1;
    size_t r_idx = 0;
    while (r_idx < len) {
        CELL cell = uforth_dict[d_idx++];
#ifdef USE_LITTLE_ENDIAN
        dst[r_idx++] = (char)(cell & 0xFF);
        if (r_idx < len) dst[r_idx++] = (char)((cell >> 8) & 0xFF);
#else
        dst[r_idx++] = (char)((cell >> 8) & 0xFF);
        if (r_idx < len) dst[r_idx++] = (char)(cell & 0xFF);
#endif
    }
    dst[len] = '\0';
    dpush(len);
}

static void fn_s_to_dict() {
    DCELL ram_addr = dpop();
    const char *s = (const char *)&uforth_ram[ram_addr];
    size_t len = strlen(s);
    CELL dict_addr = dict->here;
    dict_append((CELL)len);
    size_t i = 0;
    while (i < len) {
        CELL cell = BYTEPACK_FIRST((uint8_t)s[i++]);
        if (i < len) {
            cell |= BYTEPACK_SECOND((uint8_t)s[i++]);
        }
        dict_append(cell);
    }
    dpush(dict_addr);
}

// ── Math & Micro-Fixed Point (Core) ─────────────────────────────────────────

static void fn_star_slash() {
    DCELL n3 = dpop();
    DCELL n2 = dpop();
    DCELL n1 = dpop();
    if (n3 == 0) { forth_output("ERR: div by zero\n"); dpush(0); return; }
    int64_t prod;
    if (!__builtin_mul_overflow(n1, n2, &prod)) {
        dpush(prod / n3);
    } else {
        double res = ((double)n1 * (double)n2) / (double)n3;
        dpush((DCELL)res);
    }
}

static void fn_star_slash_mod() {
    DCELL n3 = dpop();
    DCELL n2 = dpop();
    DCELL n1 = dpop();
    if (n3 == 0) { forth_output("ERR: div by zero\n"); dpush(0); dpush(0); return; }
    int64_t prod;
    if (!__builtin_mul_overflow(n1, n2, &prod)) {
        dpush(prod % n3);
        dpush(prod / n3);
    } else {
        double q = ((double)n1 * (double)n2) / (double)n3;
        int64_t quot = (int64_t)q;
        dpush(0); // rem
        dpush(quot);
    }
}

static void fn_u_mul() {
    DCELL u2 = dpop();
    DCELL u1 = dpop();
    int64_t prod;
    if (!__builtin_mul_overflow(u1, u2, &prod)) {
        dpush(prod / 1000000LL);
    } else {
        double res = ((double)u1 * (double)u2) / 1000000.0;
        dpush((DCELL)res);
    }
}

static void fn_u_div() {
    DCELL u2 = dpop();
    DCELL u1 = dpop();
    if (u2 == 0) { forth_output("ERR: div by zero\n"); dpush(0); return; }
    int64_t prod;
    if (!__builtin_mul_overflow(u1, 1000000LL, &prod)) {
        dpush(prod / u2);
    } else {
        double res = ((double)u1 * 1000000.0) / (double)u2;
        dpush((DCELL)res);
    }
}

static void fn_u_dot() {
    DCELL u = dpop();
    if (u < 0) {
        forth_output("-");
        u = -u;
    }
    DCELL int_part = u / 1000000LL;
    DCELL frac_part = u % 1000000LL;
    char buf[48];
    int len = snprintf(buf, sizeof(buf), "%lld.%06lld", (long long)int_part, (long long)frac_part);
    while (len > 0 && buf[len - 1] == '0' && buf[len - 2] != '.') {
        buf[--len] = '\0';
    }
    buf[len++] = ' ';
    buf[len] = '\0';
    forth_output(buf);
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

    // String operations (C-strings in uforth_ram)
    forth_register("s\"", fn_s_quote); make_immediate();
    forth_register("s.", fn_s_dot);
    forth_register("type", fn_type);
    forth_register("slen", fn_slen);
    forth_register("scount", fn_scount);
    forth_register("s=", fn_seq);
    forth_register("dict>s", fn_dict_to_s);
    forth_register("s>dict", fn_s_to_dict);

    // Math & Micro-Fixed (always active in core)
    forth_register("*/", fn_star_slash);
    forth_register("*/mod", fn_star_slash_mod);
    forth_register("u*", fn_u_mul);
    forth_register("u/", fn_u_div);
    forth_register("u.", fn_u_dot);
}

void core_definitions() {
    uforth_interpret(": count dup 1+ swap 0 +c@ ;");
}
