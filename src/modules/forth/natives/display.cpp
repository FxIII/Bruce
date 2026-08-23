#include "natives_internal.h"
#include "../uforth.h"
#include "core/display.h"
#include "font8x12.h"
#include <Arduino.h>

static int _currentFontSize = 1; // 1 = 6x8px (GLCD), 2 = 8x12px (Monospace 8x12), 3 = 12x16px (GLCD x2)

static void getCharDimensions(int size, int &char_w, int &char_h) {
    if (size == 2) {
        char_w = 8;
        char_h = 12;
    } else if (size == 3) {
        char_w = 12;
        char_h = 16;
    } else {
        char_w = 6;
        char_h = 8;
    }
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
    int char_w, char_h;
    getCharDimensions(_currentFontSize, char_w, char_h);
    dpush(tftHeight / char_h);
}

static void fn_cols() {
    int char_w, char_h;
    getCharDimensions(_currentFontSize, char_w, char_h);
    dpush(tftWidth / char_w);
}

static void fn_row_height() {
    int char_w, char_h;
    getCharDimensions(_currentFontSize, char_w, char_h);
    dpush(char_h);
}

static void fn_write_line() {
    int y_pixel = dpop();
    int scroll_x = dpop();
    int line_idx = dpop();
    CELL addr = dpop();

    int char_w, char_h;
    getCharDimensions(_currentFontSize, char_w, char_h);
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

        if (_currentFontSize == 2) {
            // Render using custom 8x12 monospaced bitmap font transparently
            for (int i = 0; i < draw_len; i++) {
                char c = buf[i];
                if (c < 32 || c > 126) c = '?';
                const uint8_t *glyph = font8x12[c - 32];
                tft.drawXBitmap(i * 8, y_pixel, glyph, 8, 12, bruceConfig.priColor);
            }
        } else {
            tft.setTextFont(1);
            tft.setTextSize((_currentFontSize == 3) ? 2 : 1);
            tft.setCursor(0, y_pixel);
            tft.print(buf);
        }
    }
}

static void fn_draw_cursor() {
    int y = dpop();
    int x = dpop();

    int char_w, char_h;
    getCharDimensions(_currentFontSize, char_w, char_h);
    int cursor_h = (_currentFontSize == 3) ? 4 : 2;

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
