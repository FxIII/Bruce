#include "natives_internal.h"
#include "../uforth.h"
#include "core/configPins.h"
#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>

extern BruceConfigPins bruceConfigPins;

static HardwareSerial *_gpsSerial = nullptr;
static TinyGPSPlus _tinyGps;
static bool _gps_initialized = false;

static void update_gps_stream() {
    if (!_gps_initialized || !_gpsSerial) return;
    while (_gpsSerial->available() > 0) {
        _tinyGps.encode(_gpsSerial->read());
    }
}

static void fn_gps_init() {
    if (!_gpsSerial) {
        _gpsSerial = new HardwareSerial(2);
    }
    pinMode(bruceConfigPins.gps_bus.rx, INPUT);
    _gpsSerial->begin(
        bruceConfigPins.gpsBaudrate,
        SERIAL_8N1,
        bruceConfigPins.gps_bus.rx,
        bruceConfigPins.gps_bus.tx
    );
    _gps_initialized = true;
    dpush(0); // OK
}

static void fn_gps_fix() {
    update_gps_stream();
    dpush((_gps_initialized && _tinyGps.location.isValid()) ? 1 : 0);
}

static void fn_gps_updated() {
    update_gps_stream();
    if (_gps_initialized && _tinyGps.location.isUpdated()) {
        // Calling lat() clears the internal updated flag in TinyGPS++
        _tinyGps.location.lat();
        dpush(1);
    } else {
        dpush(0);
    }
}

static void fn_gps_pos_str() {
    update_gps_stream();
    DCELL addr = dpop();
    uint8_t *buf = (uint8_t *)&uforth_ram[addr];

    if (_gps_initialized && _tinyGps.location.isValid()) {
        char temp[48];
        snprintf(temp, sizeof(temp), "%.6f,%.6f", _tinyGps.location.lat(), _tinyGps.location.lng());
        uint8_t len = (uint8_t)strlen(temp);
        memcpy(buf, temp, len);
        buf[len] = '\0';
    } else {
        buf[0] = '\0';
    }
}

static void fn_gps_sats() {
    update_gps_stream();
    dpush(_gps_initialized ? (DCELL)_tinyGps.satellites.value() : 0);
}

static void fn_gps_lat() {
    update_gps_stream();
    DCELL val = _gps_initialized ? (DCELL)(_tinyGps.location.lat() * 1000000.0) : 0;
    dpush(val);
}

static void fn_gps_lng() {
    update_gps_stream();
    DCELL val = _gps_initialized ? (DCELL)(_tinyGps.location.lng() * 1000000.0) : 0;
    dpush(val);
}

static void fn_gps_alt() {
    update_gps_stream();
    DCELL val = _gps_initialized ? (DCELL)_tinyGps.altitude.meters() : 0;
    dpush(val);
}

static void fn_gps_speed() {
    update_gps_stream();
    DCELL val = _gps_initialized ? (DCELL)_tinyGps.speed.kmph() : 0;
    dpush(val);
}

static void fn_gps_sleep() {
    if (_gps_initialized && _gpsSerial) {
        // Send PMTK standby command
        _gpsSerial->print("$PMTK161,0*28\r\n");
        _gpsSerial->flush();
        delay(10);
        _gpsSerial->end();
        _gps_initialized = false;
    }
    dpush(0);
}

static void fn_gps_wake() {
    if (!_gps_initialized) {
        if (!_gpsSerial) {
            _gpsSerial = new HardwareSerial(2);
        }
        pinMode(bruceConfigPins.gps_bus.rx, INPUT);
        _gpsSerial->begin(
            bruceConfigPins.gpsBaudrate,
            SERIAL_8N1,
            bruceConfigPins.gps_bus.rx,
            bruceConfigPins.gps_bus.tx
        );
        _gpsSerial->print("\r\n"); // Dummy byte to wake up MTK GPS
        _gps_initialized = true;
    }
    dpush(0);
}

void gps_bindings() {
    forth_register("gps-init", fn_gps_init);
    forth_register("gps-fix?", fn_gps_fix);
    forth_register("gps-updated?", fn_gps_updated);
    forth_register("gps-pos-str", fn_gps_pos_str);
    forth_register("gps-sats", fn_gps_sats);
    forth_register("gps-lat", fn_gps_lat);
    forth_register("gps-lng", fn_gps_lng);
    forth_register("gps-alt", fn_gps_alt);
    forth_register("gps-speed", fn_gps_speed);
    forth_register("gps-sleep", fn_gps_sleep);
    forth_register("gps-wake", fn_gps_wake);
}

void gps_definitions() {
    uforth_interpret("defer on-gps");
    uforth_interpret(": _tick-gps gps-updated? if on-gps then ;");
    uforth_interpret("' _tick-gps attach-service drop");
}
