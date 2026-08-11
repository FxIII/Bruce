#include "i2c.h"
#include "natives.h"
#include "../uforth.h"
#include <Wire.h>

static int      _sda     = GROVE_SDA;
static int      _scl     = GROVE_SCL;
static TwoWire *_wire    = nullptr;
static int      _lasterr = 0;

#define I2C_GUARD if (!_wire) { _lasterr = -1; return; }

static void fn_i2c_pins_store() {
    _scl   = (int)dpop();
    _sda   = (int)dpop();
    _wire  = nullptr;
    _lasterr = 0;
}

static void fn_i2c_pins_fetch() {
    dpush(_sda);
    dpush(_scl);
}

static void fn_i2c_begin() {
    if (_sda == 8 && _scl == 9) {
        // Cardputer: Wire1 already initialized by keyboard driver — reuse it
        _wire = &Wire1;
    } else {
        Wire.begin(_sda, _scl, 100000);
        Wire.setClock(100000);
        Wire.setTimeOut(50);
        _wire = &Wire;
    }
    _lasterr = 0;
}

static void fn_i2c_scan() {
    I2C_GUARD;
    int count = 0;
    for (uint8_t a = 1; a < 127; a++) {
        _wire->beginTransmission(a);
        if (_wire->endTransmission(true) == 0) {
            dpush(a);
            count++;
        }
    }
    dpush(count);
}

static void fn_i2c_write() {
    I2C_GUARD;
    int n = (int)dpop();
    if (n < 0 || n > 32) { _lasterr = -2; dpush(_lasterr); return; }
    uint8_t buf[32];
    for (int i = n - 1; i >= 0; i--) buf[i] = (uint8_t)dpop();
    int addr = (int)dpop();
    _wire->beginTransmission((uint8_t)addr);
    _wire->write(buf, (size_t)n);
    _lasterr = (int)_wire->endTransmission(true);
    dpush(_lasterr);
}

static void fn_i2c_read() {
    I2C_GUARD;
    int n    = (int)dpop();
    int addr = (int)dpop();
    if (n < 0 || n > 32) { _lasterr = -2; dpush(_lasterr); return; }
    size_t got = _wire->requestFrom((uint8_t)addr, (uint8_t)n, (uint8_t)true);
    uint8_t buf[32];
    for (size_t i = 0; i < got && _wire->available(); i++) buf[i] = _wire->read();
    _lasterr = (got == (size_t)n) ? 0 : 1;
    dpush(_lasterr);
    for (size_t i = 0; i < got; i++) dpush(buf[i]);
}

static void fn_i2c_err() {
    dpush(_lasterr);
}

static void fn_i2c_pack() {
    uint32_t b0 = (uint32_t)dpop();
    uint32_t b1 = (uint32_t)dpop();
    uint32_t b2 = (uint32_t)dpop();
    uint32_t b3 = (uint32_t)dpop();
    dpush((b3 << 24) | (b2 << 16) | (b1 << 8) | b0);
}

static void fn_i2c_unpack() {
    uint32_t v = (uint32_t)dpop();
    dpush((v >> 24) & 0xFF);
    dpush((v >> 16) & 0xFF);
    dpush((v >>  8) & 0xFF);
    dpush( v        & 0xFF);
}

void forth_register_i2c() {
    forth_register("br.i2c.pins!", fn_i2c_pins_store);
    forth_register("br.i2c.pins@", fn_i2c_pins_fetch);
    forth_register("br.i2c.begin", fn_i2c_begin);
    forth_register("br.i2c.scan",  fn_i2c_scan);
    forth_register("br.i2c.write", fn_i2c_write);
    forth_register("br.i2c.read",  fn_i2c_read);
    forth_register("br.i2c.err",   fn_i2c_err);
    forth_register("br.i2c.pack",  fn_i2c_pack);
    forth_register("br.i2c.unpack",fn_i2c_unpack);
}
