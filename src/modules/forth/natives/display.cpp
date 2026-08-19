#include "natives_internal.h"
#include "../uforth.h"
#include "core/display.h"
#include <Arduino.h>

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

    char *line_ptr = ((char*)&uforth_ram[addr]) + line_idx * 64;

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

static void fn_draw_cursor() {
    int y = dpop();
    int x = dpop();
    tft.fillRect(x * 6, y * 8 + 6, 6, 2, bruceConfig.priColor);
}

void display_bindings() {
    forth_register("br.display.cls", fn_cls);
    forth_register("br.display.writeLine", fn_write_line);
    forth_register("br.display.drawCursor", fn_draw_cursor);
}
