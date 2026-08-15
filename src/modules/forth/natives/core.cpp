#include "core.h"
#include "natives.h"
#include "../tbforth.h"
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
    CELL addr = dpop() & 0x7FFFFFFF;

    // Clear the specific row line (240x8)
    tft.fillRect(0, y_pixel, 240, 8, bruceConfig.bgColor);

    char *line_ptr = ((char*)&tbforth_ram[addr]) + line_idx * 64;

    // Find actual length by trimming trailing spaces
    int len = 64;
    while (len > 0 && line_ptr[len - 1] == ' ') {
        len--;
    }

    if (len > scroll_x) {
        int draw_len = len - scroll_x;
        if (draw_len > 40) draw_len = 40; // Max characters visible on Cardputer

        char buf[41];
        strncpy(buf, line_ptr + scroll_x, draw_len);
        buf[draw_len] = '\0';

        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.setTextSize(1);
        tft.setCursor(0, y_pixel);
        tft.print(buf);
    }
}

static void fn_file_include() {
    CELL path_addr = dpop() & 0x7FFFFFFF;
    RAMC len = tbforth_ram[path_addr];
    char *str = (char*)&tbforth_ram[path_addr + 1];

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
        tbforth_interpret(buf);
    }
    file.close();
}

static void fn_block_read() {
    int num = dpop();
    CELL addr = dpop() & 0x7FFFFFFF;

    // 2. Open file
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/forth/blocks/%d.f", num);

    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        log_e("fn_block_read: storage not available");
        return;
    }

    // Initialize the entire buffer in RAM to spaces first
    char *base_ptr = (char*)&tbforth_ram[addr];
    for (int i = 0; i < 16; i++) {
        memset(base_ptr + i * 64, ' ', 64);
    }

    if (!fs->exists(filepath)) {
        log_w("fn_block_read: file '%s' does not exist, using empty block", filepath);
        return;
    }

    File file = fs->open(filepath, "r");
    if (!file) {
        log_e("fn_block_read: failed to open '%s'", filepath);
        return;
    }

    int line_idx = 0;
    while (file.available() && line_idx < 16) {
        String line = file.readStringUntil('\n');
        // Remove trailing carriage returns/spaces
        while (line.length() > 0 && (line.endsWith("\r") || line.endsWith("\n"))) {
            line.remove(line.length() - 1);
        }

        int len = line.length();
        if (len > 64) len = 64;

        char *line_ptr = base_ptr + line_idx * 64;
        memcpy(line_ptr, line.c_str(), len);
        line_idx++;
    }
    file.close();
    log_d("fn_block_read: loaded '%s'", filepath);
}

static void fn_block_write() {
    int num = dpop();
    CELL addr = dpop() & 0x7FFFFFFF;

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/forth/blocks/%d.f", num);

    FS *fs = nullptr;
    if (!getFsStorage(fs)) {
        log_e("fn_block_write: storage not available");
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
        log_e("fn_block_write: failed to open '%s' for writing", filepath);
        return;
    }

    char *base_ptr = (char*)&tbforth_ram[addr];
    for (int i = 0; i < 16; i++) {
        char *line_ptr = base_ptr + i * 64;

        // Trim trailing spaces for cleaner text files on disk
        int trim_len = 64;
        while (trim_len > 0 && line_ptr[trim_len - 1] == ' ') {
            trim_len--;
        }

        char buf[65];
        memcpy(buf, line_ptr, trim_len);
        buf[trim_len] = '\0';

        file.println(buf);
    }
    file.close();
    log_d("fn_block_save: saved '%s'", filepath);
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
                dpush((unsigned char)ks.word[0]);
                return;
            }
        }
        delay(10);
    }
}

void forth_register_core() {
    forth_register("br.display.cls", fn_cls);
    forth_register("br.display.writeLine", fn_write_line);
    forth_register("br.display.drawCursor", fn_draw_cursor);
    forth_register("br.file.include", fn_file_include);
    forth_register("br.block.read", fn_block_read);
    forth_register("br.block.write", fn_block_write);
    forth_register("key", fn_key);
}
