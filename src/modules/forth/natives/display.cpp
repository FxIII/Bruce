#include "natives_internal.h"
#include "../uforth.h"
#include "../forth_repl.h"
#include "core/display.h"
#include "font8x12.h"
#include "font10x18.h"
#include <Arduino.h>

static int _currentFontSize = 1; 

static void getCharDimensions(int size, int &char_w, int &char_h) {
    switch (size) {
        case 1:  char_w = 6;  char_h = 8;  break; // GLCD 1x
        case 2:  char_w = 8;  char_h = 12; break; // Font8x12 1x
        case 3:  char_w = 10; char_h = 18; break; // Terminus 1x
        case 4:  char_w = 12; char_h = 16; break; // GLCD 2x
        case 5:  char_w = 16; char_h = 24; break; // Font8x12 2x
        case 6:  char_w = 18; char_h = 24; break; // GLCD 3x
        case 7:  char_w = 20; char_h = 36; break; // Terminus 2x
        case 8:  char_w = 24; char_h = 32; break; // GLCD 4x
        case 9:  char_w = 24; char_h = 36; break; // Font8x12 3x
        case 10: char_w = 30; char_h = 54; break; // Terminus 3x
        case 11: char_w = 32; char_h = 48; break; // Font8x12 4x
        default: char_w = 6;  char_h = 8;  break;
    }
}

static void drawGlyph8x12(int x_base, int y_base, const uint8_t glyph[12], int scale, uint16_t color) {
    if (scale == 1) {
        tft.drawXBitmap(x_base, y_base, glyph, 8, 12, color);
        return;
    }
    for (int r = 0; r < 12; r++) {
        uint8_t bits = glyph[r];
        if (!bits) continue;
        int y = y_base + r * scale;
        int col = 0;
        while (col < 8) {
            if (bits & (1 << col)) {
                int start_col = col;
                while (col < 8 && (bits & (1 << col))) {
                    col++;
                }
                int run_len = col - start_col;
                tft.fillRect(x_base + start_col * scale, y, run_len * scale, scale, color);
            } else {
                col++;
            }
        }
    }
}

static void drawGlyph10x18(int x_base, int y_base, const uint16_t glyph[18], int scale, uint16_t color) {
    for (int r = 0; r < 18; r++) {
        uint16_t bits = glyph[r];
        if (!bits) continue;
        int y = y_base + r * scale;
        int col = 0;
        while (col < 10) {
            if (bits & (1 << (9 - col))) {
                int start_col = col;
                while (col < 10 && (bits & (1 << (9 - col)))) {
                    col++;
                }
                int run_len = col - start_col;
                tft.fillRect(x_base + start_col * scale, y, run_len * scale, scale, color);
            } else {
                col++;
            }
        }
    }
}

static void drawTextLine(const char *buf, int len, int x_pixel, int y_pixel, int char_w) {
    int glcd_scale = 0;
    if (_currentFontSize == 1) glcd_scale = 1;
    else if (_currentFontSize == 4) glcd_scale = 2;
    else if (_currentFontSize == 6) glcd_scale = 3;
    else if (_currentFontSize == 8) glcd_scale = 4;

    if (glcd_scale > 0) {
        tft.setTextFont(1);
        tft.setTextSize(glcd_scale);
        tft.setCursor(x_pixel, y_pixel);
        tft.print(buf);
        return;
    }

    int f8_scale = 0;
    if (_currentFontSize == 2) f8_scale = 1;
    else if (_currentFontSize == 5) f8_scale = 2;
    else if (_currentFontSize == 9) f8_scale = 3;
    else if (_currentFontSize == 11) f8_scale = 4;

    if (f8_scale > 0) {
        for (int i = 0; i < len; i++) {
            char c = buf[i];
            if (c < 32 || c > 126) c = '?';
            drawGlyph8x12(x_pixel + i * char_w, y_pixel, font8x12[c - 32], f8_scale, bruceConfig.priColor);
        }
        return;
    }

    int f10_scale = 0;
    if (_currentFontSize == 3) f10_scale = 1;
    else if (_currentFontSize == 7) f10_scale = 2;
    else if (_currentFontSize == 10) f10_scale = 3;

    if (f10_scale > 0) {
        for (int i = 0; i < len; i++) {
            char c = buf[i];
            if (c < 32 || c > 126) c = '?';
            drawGlyph10x18(x_pixel + i * char_w, y_pixel, font10x18[c - 32], f10_scale, bruceConfig.priColor);
        }
        return;
    }
}

static void fn_cls() {
    forth_cls();
}

static void fn_render() {
    ConsoleWidget* widget = getActiveConsoleWidget();
    if (widget) {
        widget->render();
    }
}

static void fn_font_size_set() {
    int size = dpop();
    if (size < 1) size = 1;
    if (size > 11) size = 11;
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
        drawTextLine(buf, draw_len, 0, y_pixel, char_w);
    }
}

static void fn_draw_cursor() {
    int y = dpop();
    int x = dpop();

    int char_w, char_h;
    getCharDimensions(_currentFontSize, char_w, char_h);
    int cursor_h = (char_h >= 36) ? 4 : ((char_h >= 24) ? 3 : 2);

    int y_pixel = y * char_h;
    tft.fillRect(x * char_w, y_pixel + char_h - cursor_h, char_w, cursor_h, bruceConfig.priColor);
}

static int get_line_rows(CELL addr) {
    int char_w, char_h;
    getCharDimensions(_currentFontSize, char_w, char_h);
    int max_chars = tftWidth / char_w;
    if (max_chars <= 0) max_chars = 1;

    char *line_ptr = (char*)&uforth_ram[addr];
    int len = strnlen(line_ptr, 64);
    int rows = (len + max_chars - 1) / max_chars;
    return (rows < 1) ? 1 : rows;
}

static void fn_line_rows() {
    CELL addr = dpop();
    dpush(get_line_rows(addr));
}

static void fn_write_line_ml() {
    int y_pixel = dpop();
    CELL addr = dpop();

    int char_w, char_h;
    getCharDimensions(_currentFontSize, char_w, char_h);
    int max_chars = tftWidth / char_w;
    if (max_chars <= 0) max_chars = 1;

    int rows = get_line_rows(addr);
    tft.fillRect(0, y_pixel, tftWidth, rows * char_h, bruceConfig.bgColor);

    char *line_ptr = (char*)&uforth_ram[addr];
    int len = strnlen(line_ptr, 64);

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    int cur_y = y_pixel;
    int offset = 0;

    for (int r = 0; r < rows; r++) {
        int chunk_len = len - offset;
        if (chunk_len > max_chars) chunk_len = max_chars;

        char buf[128];
        if (chunk_len > 0) {
            memcpy(buf, line_ptr + offset, chunk_len);
            buf[chunk_len] = '\0';
        } else {
            buf[0] = '\0';
        }

        if (chunk_len > 0) {
            drawTextLine(buf, chunk_len, 0, cur_y, char_w);
        }

        cur_y += char_h;
        offset += max_chars;
    }
}

void display_bindings() {
    forth_register("cls", fn_cls);                      // cls             ( -- )
    forth_register("disp-refresh", fn_render);          // disp-refresh    ( -- )
    forth_register("disp-cursor", fn_draw_cursor);      // disp-cursor     ( col row -- )
    forth_register("disp-write", fn_write_line);        // disp-write      ( addr line_idx scroll_x y_pixel -- )
    forth_register("disp-write-ml", fn_write_line_ml);  // disp-write-ml   ( addr y_pixel -- )
    forth_register("disp-line-rows", fn_line_rows);     // disp-line-rows  ( addr -- rows )
    forth_register("disp-font!", fn_font_size_set);     // disp-font!      ( size -- )
    forth_register("disp-font@", fn_font_size_get);     // disp-font@      ( -- size )
    forth_register("disp-rows", fn_rows);               // disp-rows       ( -- count )
    forth_register("disp-cols", fn_cols);               // disp-cols       ( -- count )
    forth_register("disp-row-height", fn_row_height);   // disp-row-height ( -- height )
}

void display_definitions() {
}
