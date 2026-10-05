#include "natives_internal.h"
#include "../uforth.h"
#include "core/bus_HAL.h"
#include "core/configPins.h"
#include <Arduino.h>
#include <Wire.h>

static TwoWire *_i2cWire = nullptr;
static bool _i2cAcquired = false;
static int8_t _customSda = -1;
static int8_t _customScl = -1;

// i2c-pins ( sda scl -- )
static void fn_i2c_pins() {
    _customScl = (int8_t)dpop();
    _customSda = (int8_t)dpop();
}

// i2c-porta ( -- )
static void fn_i2c_porta() {
    _customSda = -1;
    _customScl = -1;
}

// i2c-init ( clk -- ok? )
static void fn_i2c_init() {
    int32_t clk = (int32_t)dpop();

    if (_i2cWire && _i2cAcquired) {
        releaseI2CBus();
    }
    _i2cWire     = nullptr;
    _i2cAcquired = false;

    if (_customSda >= 0 && _customScl >= 0) {
        _i2cWire = acquireI2CBus(_customSda, _customScl);
    } else {
        _i2cWire = acquireI2CBus();
    }
    _i2cAcquired = (_i2cWire != nullptr);

    if (_i2cWire) {
        _i2cWire->setClock(clk * 1000);
        _i2cWire->setTimeOut(50);
        dpush(1);
    } else {
        dpush(0);
    }
}

static void fn_i2c_deinit() {
    if (_i2cWire && _i2cAcquired) {
        releaseI2CBus();
    }
    _i2cWire     = nullptr;
    _i2cAcquired = false;
}

static void fn_i2c_ping() {
    uint8_t addr = (uint8_t)dpop();
    if (!_i2cWire) {
        dpush(0);
        return;
    }
    _i2cWire->beginTransmission(addr);
    uint8_t err = _i2cWire->endTransmission(true);
    dpush(err == 0 ? 1 : 0);
}

static void fn_i2c_scan() {
    if (!_i2cWire) {
        dpush(0);
        return;
    }
    uint8_t found[128];
    int count = 0;
    for (uint8_t a = 1; a < 127; a++) {
        _i2cWire->beginTransmission(a);
        if (_i2cWire->endTransmission(true) == 0) {
            found[count++] = a;
        }
    }
    for (int i = 0; i < count; i++) {
        dpush(found[i]);
    }
    dpush(count);
}

// i2c@ ( reg len  addr -- dcell ok? )
// Reads 1-8 bytes from register reg at device addr.
// Bytes are packed into dcell LSB-first (byte 0 -> bits 0-7, byte 1 -> bits 8-15, ...).
// ok? = 1 success, 0 = short read, negative = Wire error code.
static void fn_i2c_fetch() {
    uint8_t addr = (uint8_t)dpop();
    uint8_t len  = (uint8_t)dpop();
    uint8_t reg  = (uint8_t)dpop();

    if (!_i2cWire || len == 0 || len > 8) {
        dpush(0);
        dpush(-6);  // bus not ready or bad len
        return;
    }

    _i2cWire->beginTransmission(addr);
    _i2cWire->write(reg);
    uint8_t err = _i2cWire->endTransmission(); // repeated start
    if (err != 0) {
        dpush(0);
        dpush(-err); // 2=nack addr, 3=nack data, 4=other, 5=timeout
        return;
    }

    size_t got = _i2cWire->requestFrom((uint8_t)addr, (uint8_t)len);
    DCELL data = 0;
    for (size_t i = 0; i < got; i++) {
        data |= ((DCELL)(_i2cWire->read() & 0xFF)) << (i * 8);
    }
    dpush(data);
    dpush(got == len ? 1 : 0);
}

// i2c! ( value reg len addr -- ok? )
// Writes 1-8 bytes to register reg at device addr.
// Bytes are extracted from dcell LSB-first (bits 0-7 -> byte 0, bits 8-15 -> byte 1, ...).
// ok? = 1 success, negative = Wire error code.
static void fn_i2c_store() {
    uint8_t addr = (uint8_t)dpop();
    uint8_t len  = (uint8_t)dpop();
    uint8_t reg  = (uint8_t)dpop();
    DCELL   data = dpop();

    if (!_i2cWire || len == 0 || len > 8) {
        dpush(-6);  // bus not ready or bad len
        return;
    }

    _i2cWire->beginTransmission(addr);
    _i2cWire->write(reg);
    for (uint8_t i = 0; i < len; i++) {
        _i2cWire->write((uint8_t)((data >> (i * 8)) & 0xFF));
    }
    uint8_t err = _i2cWire->endTransmission(true);
    dpush(err == 0 ? 1 : -(DCELL)err);
}

// i2c-reg ( c-addr reg len addr -- ok? )
static void fn_i2c_reg() {
    uint8_t addr = (uint8_t)dpop();
    size_t len   = (size_t)dpop();
    uint8_t reg  = (uint8_t)dpop();
    CELL c_addr  = (CELL)dpop();

    if (!_i2cWire || len == 0) {
        dpush(0);
        return;
    }
    uint8_t *buf = (uint8_t *)&uforth_ram[c_addr];

    _i2cWire->beginTransmission(addr);
    _i2cWire->write(reg);
    uint8_t err = _i2cWire->endTransmission();
    if (err != 0) {
        dpush(0);
        return;
    }
    size_t got = _i2cWire->requestFrom((uint8_t)addr, (uint8_t)len);
    for (size_t i = 0; i < got; i++) {
        buf[i] = (uint8_t)_i2cWire->read();
    }
    dpush(got == len ? 1 : 0);
}

// i2c-read ( c-addr len addr -- ok? )
static void fn_i2c_read() {
    uint8_t addr = (uint8_t)dpop();
    size_t len   = (size_t)dpop();
    CELL c_addr  = (CELL)dpop();

    if (!_i2cWire || len == 0) {
        dpush(0);
        return;
    }
    uint8_t *buf = (uint8_t *)&uforth_ram[c_addr];

    size_t got = _i2cWire->requestFrom((uint8_t)addr, (uint8_t)len);
    for (size_t i = 0; i < got; i++) {
        buf[i] = (uint8_t)_i2cWire->read();
    }
    dpush(got == len ? 1 : 0);
}

static void fn_i2c_write() {
    uint8_t addr = (uint8_t)dpop();
    size_t len   = (size_t)dpop();
    CELL c_addr  = (CELL)dpop();

    if (!_i2cWire) {
        dpush(0);
        return;
    }
    uint8_t *buf = (uint8_t *)&uforth_ram[c_addr];

    _i2cWire->beginTransmission(addr);
    _i2cWire->write(buf, len);
    uint8_t err = _i2cWire->endTransmission(true);
    dpush(err == 0 ? 1 : 0);
}

void i2c_bindings() {
    forth_register("i2c-pins",   fn_i2c_pins);
    forth_register("i2c-porta",  fn_i2c_porta);
    forth_register("i2c-init",   fn_i2c_init);
    forth_register("i2c-deinit", fn_i2c_deinit);
    forth_register("i2c-ping?",  fn_i2c_ping);
    forth_register("i2c-scan",   fn_i2c_scan);
    forth_register("i2c@",       fn_i2c_fetch);
    forth_register("i2c!",       fn_i2c_store);
    forth_register("i2c-reg",    fn_i2c_reg);
    forth_register("i2c-read",   fn_i2c_read);
    forth_register("i2c-write",  fn_i2c_write);
}

void i2c_definitions() {
}
