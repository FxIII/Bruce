#include "natives_internal.h"
#include "../uforth.h"
#include "core/display.h"
#include <Arduino.h>

static int _currentFontSize = 1; // 1 = 8px (1.0x), 2 = 12px (1.5x), 3 = 16px (2.0x)

static float getFontScale(int size) {
    if (size == 2) return 1.5f;
    if (size == 3) return 2.0f;
    return 1.0f;
}

static void fn_cls() {
    forth_cls();
}

static void fn_font_size_set() {
    int size = dpop();
    if (size < 1) size = 1;
    if (size > 3) size = 3;
    _currentFontSize = size;
}

static void fn_font_size_get() {
    dpush(_currentFontSize);
}

static void fn_rows() {
    float scale = getFontScale(_currentFontSize);
    int char_h = (int)(8.0f * scale);
    dpush(tftHeight / char_h);
}

static void fn_cols() {
    float scale = getFontScale(_currentFontSize);
    int char_w = (int)(6.0f * scale);
    dpush(tftWidth / char_w);
}

static void fn_row_height() {
    float scale = getFontScale(_currentFontSize);
    int char_h = (int)(8.0f * scale);
    dpush(char_h);
}

static void fn_write_line() {
    int y_pixel = dpop();
    int scroll_x = dpop();
    int line_idx = dpop();
    CELL addr = dpop();

    float scale = getFontScale(_currentFontSize);
    int char_w = (int)(6.0f * scale);
    int char_h = (int)(8.0f * scale);
    int max_chars = tftWidth / char_w;

    // Clear the specific row line (240 x char_h)
    tft.fillRect(0, y_pixel, tftWidth, char_h, bruceConfig.bgColor);

    char *line_ptr = ((char*)&uforth_ram[addr]) + line_idx * 64;

    // Find actual length by trimming trailing spaces
    int len = 64;
    while (len > 0 && line_ptr[len - 1] == ' ') {
        len--;
    }

    if (len > scroll_x) {
        int draw_len = len - scroll_x;
        if (draw_len > max_chars) draw_len = max_chars;

        char buf[128];
        strncpy(buf, line_ptr + scroll_x, draw_len);
        buf[draw_len] = '\0';

        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.setTextSize(scale);
        tft.setCursor(0, y_pixel);
        tft.print(buf);
    }
}

static void fn_draw_cursor() {
    int y = dpop();
    int x = dpop();

    float scale = getFontScale(_currentFontSize);
    int char_w = (int)(6.0f * scale);
    int char_h = (int)(8.0f * scale);
    int cursor_h = (int)(2.0f * scale);

    int y_pixel = y * char_h;
    tft.fillRect(x * char_w, y_pixel + char_h - cursor_h, char_w, cursor_h, bruceConfig.priColor);
}

void display_bindings() {
    forth_register("br.display.cls", fn_cls);
    forth_register("br.display.writeLine", fn_write_line);
    forth_register("br.display.drawCursor", fn_draw_cursor);
    forth_register("br.display.fontSize!", fn_font_size_set);
    forth_register("br.display.fontSize?", fn_font_size_get);
    forth_register("br.display.rows", fn_rows);
    forth_register("br.display.cols", fn_cols);
    forth_register("br.display.rowHeight", fn_row_height);
}
