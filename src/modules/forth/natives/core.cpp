#include "core.h"
#include "natives.h"
#include "../uforth.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include <Arduino.h>
#include <FS.h>

extern "C" void make_immediate(void);

static void fn_cls() {
    forth_cls();
}

static void fn_write_line() {
    int y_pixel = dpop();
    int scroll_x = dpop();
    int line_idx = dpop();
    CELL addr = dpop();

    // Clear the specific row line (240x8)
    tft.fillRect(0, y_pixel, 240, 8, bruceConfig.bgColor);

    // Access the counted string at lines-buf + line_idx * 17
    CELL cell_idx = addr + line_idx * 17;
    DCELL len = uforth_ram[cell_idx];
    char *str = (char*)&uforth_ram[cell_idx + 1];

    if (len > scroll_x) {
        int draw_len = len - scroll_x;
        if (draw_len > 40) draw_len = 40; // Max characters visible on Cardputer

        char buf[41];
        strncpy(buf, str + scroll_x, draw_len);
        buf[draw_len] = '\0';

        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.setTextSize(1);
        tft.setCursor(0, y_pixel);
        tft.print(buf);
    }
}

static void fn_file_include() {
    CELL path_addr = dpop();
    DCELL len = uforth_ram[path_addr];
    char *str = (char*)&uforth_ram[path_addr + 1];

    char filepath[128];
    if (len >= sizeof(filepath)) len = sizeof(filepath) - 1;
    strncpy(filepath, str, len);
    filepath[len] = '\0';

    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        log_e("fn_file_include: storage not available");
        return;
    }

    File file = fs->open(filepath, "r");
    if (!file) {
        log_e("fn_file_include: failed to open '%s'", filepath);
        return;
    }

    log_d("fn_file_include: interpreting '%s'", filepath);
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.isEmpty() || line.startsWith("\\")) continue; // Skip comments and empty lines

        char buf[256];
        strncpy(buf, line.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        uforth_interpret(buf);
    }
    file.close();
}

static void fn_block_load() {
    int num = dpop();
    CELL addr = dpop();

    // 1. Clear lines-buf (16 lines of 17 cells each)
    for (int i = 0; i < 16; i++) {
        uforth_ram[addr + i * 17] = 0;
        char *str = (char*)&uforth_ram[addr + i * 17 + 1];
        memset(str, ' ', 64);
    }

    // 2. Open file
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/forth/blocks/%d.f", num);

    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        log_e("fn_block_load: storage not available");
        return;
    }

    if (!fs->exists(filepath)) {
        log_w("fn_block_load: file '%s' does not exist, using empty block", filepath);
        return;
    }

    File file = fs->open(filepath, "r");
    if (!file) {
        log_e("fn_block_load: failed to open '%s'", filepath);
        return;
    }

    int line_idx = 0;
    while (file.available() && line_idx < 16) {
        String line = file.readStringUntil('\n');
        // Remove trailing carriage returns/spaces
        while (line.length() > 0 && (line.endsWith("\r") || line.endsWith("\n"))) {
            line.remove(line.length() - 1);
        }

        CELL line_offset = addr + line_idx * 17;
        int len = line.length();
        if (len > 64) len = 64;

        uforth_ram[line_offset] = len;
        char *str = (char*)&uforth_ram[line_offset + 1];
        memcpy(str, line.c_str(), len);
        // Pad the rest with spaces
        if (len < 64) {
            memset(str + len, ' ', 64 - len);
        }
        line_idx++;
    }
    file.close();
    log_d("fn_block_load: loaded '%s'", filepath);
}

static void fn_block_save() {
    int num = dpop();
    CELL addr = dpop();

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/forth/blocks/%d.f", num);

    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        log_e("fn_block_save: storage not available");
        return;
    }

    // Ensure directory /forth/blocks exists
    if (!fs->exists("/forth")) {
        fs->mkdir("/forth");
    }
    if (!fs->exists("/forth/blocks")) {
        fs->mkdir("/forth/blocks");
    }

    File file = fs->open(filepath, "w");
    if (!file) {
        log_e("fn_block_save: failed to open '%s' for writing", filepath);
        return;
    }

    for (int i = 0; i < 16; i++) {
        CELL line_offset = addr + i * 17;
        DCELL len = uforth_ram[line_offset];
        char *str = (char*)&uforth_ram[line_offset + 1];

        // Trim trailing spaces for cleaner text files on disk
        int trim_len = len;
        while (trim_len > 0 && str[trim_len - 1] == ' ') {
            trim_len--;
        }

        char buf[65];
        memcpy(buf, str, trim_len);
        buf[trim_len] = '\0';

        file.println(buf);
    }
    file.close();
    log_d("fn_block_save: saved '%s'", filepath);
}

static void fn_set_char() {
    int offset = dpop();
    CELL base = dpop();
    int c = dpop();
    char *str = (char*)&uforth_ram[base];
    str[offset] = c;
}

static void fn_set_len() {
    CELL base = dpop();
    int len = dpop();
    uforth_ram[base] = len;
}

static void fn_draw_cursor() {
    int y = dpop();
    int x = dpop();
    tft.fillRect(x * 6, y * 8 + 6, 6, 2, bruceConfig.priColor);
}

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

static void fn_block_interpret() {
    int num = dpop();
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/forth/blocks/%d.f", num);

    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        forth_output("Storage not available\n");
        return;
    }

    if (!fs->exists(filepath)) {
        forth_output("Block file not found: ");
        forth_output(filepath);
        forth_output("\n");
        return;
    }

    File file = fs->open(filepath, "r");
    if (!file) {
        forth_output("Failed to open block file\n");
        return;
    }

    int line_num = 1;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.isEmpty() || line.startsWith("\\")) {
            line_num++;
            continue;
        }

        char buf[256];
        strncpy(buf, line.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        uforth_stat st = uforth_interpret(buf);
        if (st != UFORTH_OK) {
            char errMsg[192];
            snprintf(errMsg, sizeof(errMsg), "Error %d on line %d: %s\n", (int)st, line_num, buf);
            forth_output(errMsg);
            file.close();
            uforth_abort_request(ABORT_NAW);
            uforth_abort();
            return;
        }
        line_num++;
    }
    file.close();
}

void forth_register_core() {
    forth_register("emit", fn_emit);
    forth_register("cr", fn_cr);
    forth_register(".", fn_dot);
    forth_register("words", fn_words);
    forth_register("\\", fn_line_comment); make_immediate();
    forth_register("(", fn_paren_comment); make_immediate();

    forth_register("br.display.cls", fn_cls);
    forth_register("br.display.writeLine", fn_write_line);
    forth_register("br.display.setChar", fn_set_char);
    forth_register("br.display.setLen", fn_set_len);
    forth_register("br.file.include", fn_file_include);
    forth_register("br.block.load", fn_block_load);
    forth_register("br.block.save", fn_block_save);

    forth_register("load", fn_block_interpret);

    forth_register("br.display.drawCursor", fn_draw_cursor);
    forth_register("key", fn_key);
}
