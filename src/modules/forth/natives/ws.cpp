#include "ws.h"
#include "natives.h"
#include <FastLED.h>
#include <stdlib.h>
#include <string.h>

static CRGB    *_leds       = nullptr;
static int      _n          = 0;
static uint8_t  _brightness = 255;

#define WS_GUARD if (!_leds) return

static void _init_strip(int pin, int n) {
    switch (pin) {
        case 1: FastLED.addLeds<WS2812B, 1, GRB>(_leds, n); break;
        case 2: FastLED.addLeds<WS2812B, 2, GRB>(_leds, n); break;
        default: return;
    }
}

static void fn_ws_begin() {
    int n   = (int)dpop();
    int pin = (int)dpop();
    if (_leds) return;
    if (pin != 1 && pin != 2) return;
    if (n <= 0) return;
    _leds = new CRGB[n];
    memset(_leds, 0, n * sizeof(CRGB));
    _n = n;
    _init_strip(pin, n);
}

static void fn_ws_set() {
    WS_GUARD;
    uint32_t color = (uint32_t)dpop();
    int      i     = (int)dpop();
    if (i < 0 || i >= _n) return;
    _leds[i] = CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

static void fn_ws_get() {
    if (!_leds) { dpush(0); return; }
    int i = (int)dpop();
    if (i < 0 || i >= _n) { dpush(0); return; }
    dpush(((uint32_t)_leds[i].r << 16) | ((uint32_t)_leds[i].g << 8) | _leds[i].b);
}

static void fn_ws_show() {
    WS_GUARD;
    FastLED.setBrightness(_brightness);
    FastLED.show();
}

static void fn_ws_clear() {
    WS_GUARD;
    memset(_leds, 0, _n * sizeof(CRGB));
    FastLED.show();
}

static void fn_ws_brightness() {
    _brightness = (uint8_t)dpop();
}

static void fn_ws_multiset() {
    WS_GUARD;
    uint32_t color = (uint32_t)dpop();
    int      count = (int)dpop();
    int      start = (int)dpop();
    if (start < 0) start = 0;
    if (start + count > _n) count = _n - start;
    CRGB c((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
    for (int i = start; i < start + count; i++) _leds[i] = c;
}

static void fn_ws_shiftr() {
    WS_GUARD;
    int n     = (int)dpop();
    int count = (int)dpop();
    int start = (int)dpop();
    if (start < 0 || n <= 0 || count <= 0) return;
    if (start + count > _n) count = _n - start;
    if (n >= count) { memset(_leds + start, 0, count * sizeof(CRGB)); return; }
    memmove(_leds + start + n, _leds + start, (count - n) * sizeof(CRGB));
    memset(_leds + start, 0, n * sizeof(CRGB));
}

static void fn_ws_shiftl() {
    WS_GUARD;
    int n     = (int)dpop();
    int count = (int)dpop();
    int start = (int)dpop();
    if (start < 0 || n <= 0 || count <= 0) return;
    if (start + count > _n) count = _n - start;
    if (n >= count) { memset(_leds + start, 0, count * sizeof(CRGB)); return; }
    memmove(_leds + start, _leds + start + n, (count - n) * sizeof(CRGB));
    memset(_leds + start + count - n, 0, n * sizeof(CRGB));
}

static void fn_ws_pack() {
    uint32_t b = (uint32_t)dpop();
    uint32_t g = (uint32_t)dpop();
    uint32_t r = (uint32_t)dpop();
    dpush((r << 16) | (g << 8) | b);
}

static void fn_ws_unpack() {
    uint32_t color = (uint32_t)dpop();
    dpush((color >> 16) & 0xFF);
    dpush((color >> 8)  & 0xFF);
    dpush(color         & 0xFF);
}

void forth_register_ws() {
    forth_register("br.ws.begin",      fn_ws_begin);
    forth_register("br.ws.set",        fn_ws_set);
    forth_register("br.ws.get",        fn_ws_get);
    forth_register("br.ws.show",       fn_ws_show);
    forth_register("br.ws.clear",      fn_ws_clear);
    forth_register("br.ws.brightness", fn_ws_brightness);
    forth_register("br.ws.multiset",   fn_ws_multiset);
    forth_register("br.ws.shiftr",     fn_ws_shiftr);
    forth_register("br.ws.shiftl",     fn_ws_shiftl);
    forth_register("br.ws.pack",       fn_ws_pack);
    forth_register("br.ws.unpack",     fn_ws_unpack);
}
