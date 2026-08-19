#include "lora.h"
#include "natives_internal.h"
#include "../uforth.h"
#include <Arduino.h>

#if !defined(LITE_VERSION)
#include <RadioLib.h>

extern bool startLoraRadio(float bandMHz);
extern bool sendLoraMessage(String &payload);
extern volatile bool loraPacketReceived;
extern bool intlora;
extern volatile bool loraInterruptEnabled;
extern SX1276 *lora1276;
extern SX1262 *lora1262;
enum class LoRaRadioVariant { SX1276, SX1262 };
extern LoRaRadioVariant loraRadioVariant;

static void fn_lora_init() {
    DCELL val = dpop();
    float freq = (float)val;
    if (freq > 10000.0f) {
        freq = freq / 100.0f; // e.g. 86800 -> 868.00 MHz, 86810 -> 868.10 MHz
    } else if (freq <= 0.0f) {
        freq = 868.0f; // Default EU868 frequency (868.0 MHz)
    }
    bool ok = startLoraRadio(freq);
    dpush(ok ? 0 : 1);
}

static void fn_lora_available() {
    dpush((loraPacketReceived && intlora) ? 1 : 0);
}

static void fn_lora_read() {
    DCELL addr = dpop();
    uint8_t *buf = (uint8_t *)&uforth_ram[addr];

    if (!loraPacketReceived || !intlora) {
        buf[0] = 0;
        buf[1] = '\0';
        return;
    }

    loraInterruptEnabled = false;
    loraPacketReceived = false;

    String incoming;
    int state = (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262)
                    ? lora1262->readData(incoming)
                    : (lora1276 ? lora1276->readData(incoming) : -1);

    if (state == RADIOLIB_ERR_NONE) {
        uint8_t len = incoming.length() > 250 ? 250 : (uint8_t)incoming.length();
        buf[0] = len;
        memcpy(&buf[1], incoming.c_str(), len);
        buf[1 + len] = '\0';
    } else {
        buf[0] = 0;
        buf[1] = '\0';
    }

    if (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262) {
        lora1262->startReceive();
    } else if (lora1276) {
        lora1276->startReceive();
    }
    loraInterruptEnabled = true;
}

static void fn_lora_write() {
    DCELL addr = dpop();
    uint8_t *buf = (uint8_t *)&uforth_ram[addr];
    uint8_t len = buf[0];
    if (len == 0) {
        dpush(0);
        return;
    }
    String payload = String((const char *)&buf[1], len);
    bool ok = sendLoraMessage(payload);
    dpush(ok ? 0 : 1);
}

static void fn_lora_sf() {
    DCELL sf = dpop();
    if (!intlora) { dpush(1); return; }
    loraInterruptEnabled = false;
    int state = (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262)
                    ? lora1262->setSpreadingFactor((uint8_t)sf)
                    : (lora1276 ? lora1276->setSpreadingFactor((uint8_t)sf) : -1);
    if (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262) {
        lora1262->startReceive();
    } else if (lora1276) {
        lora1276->startReceive();
    }
    loraInterruptEnabled = true;
    dpush(state == RADIOLIB_ERR_NONE ? 0 : 1);
}

static void fn_lora_bw() {
    DCELL val = dpop();
    float bw_khz = (float)val;
    if (!intlora) { dpush(1); return; }
    loraInterruptEnabled = false;
    int state = (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262)
                    ? lora1262->setBandwidth(bw_khz)
                    : (lora1276 ? lora1276->setBandwidth(bw_khz) : -1);
    if (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262) {
        lora1262->startReceive();
    } else if (lora1276) {
        lora1276->startReceive();
    }
    loraInterruptEnabled = true;
    dpush(state == RADIOLIB_ERR_NONE ? 0 : 1);
}

static void fn_lora_cr() {
    DCELL cr = dpop();
    if (!intlora) { dpush(1); return; }
    loraInterruptEnabled = false;
    int state = (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262)
                    ? lora1262->setCodingRate((uint8_t)cr)
                    : (lora1276 ? lora1276->setCodingRate((uint8_t)cr) : -1);
    if (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262) {
        lora1262->startReceive();
    } else if (lora1276) {
        lora1276->startReceive();
    }
    loraInterruptEnabled = true;
    dpush(state == RADIOLIB_ERR_NONE ? 0 : 1);
}

static void fn_lora_power() {
    DCELL power = dpop();
    if (!intlora) { dpush(1); return; }
    loraInterruptEnabled = false;
    int state = (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262)
                    ? lora1262->setOutputPower((int8_t)power)
                    : (lora1276 ? lora1276->setOutputPower((int8_t)power) : -1);
    if (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262) {
        lora1262->startReceive();
    } else if (lora1276) {
        lora1276->startReceive();
    }
    loraInterruptEnabled = true;
    dpush(state == RADIOLIB_ERR_NONE ? 0 : 1);
}

static void fn_lora_preamble() {
    DCELL preamble = dpop();
    if (!intlora) { dpush(1); return; }
    loraInterruptEnabled = false;
    int state = (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262)
                    ? lora1262->setPreambleLength((uint16_t)preamble)
                    : (lora1276 ? lora1276->setPreambleLength((uint16_t)preamble) : -1);
    if (loraRadioVariant == LoRaRadioVariant::SX1262 && lora1262) {
        lora1262->startReceive();
    } else if (lora1276) {
        lora1276->startReceive();
    }
    loraInterruptEnabled = true;
    dpush(state == RADIOLIB_ERR_NONE ? 0 : 1);
}

void lora_bindings() {
    forth_register("lora-init", fn_lora_init);
    forth_register("lora-available?", fn_lora_available);
    forth_register("lora-read", fn_lora_read);
    forth_register("lora-write", fn_lora_write);

    forth_register("lora-sf!", fn_lora_sf);
    forth_register("lora-bw!", fn_lora_bw);
    forth_register("lora-cr!", fn_lora_cr);
    forth_register("lora-power!", fn_lora_power);
    forth_register("lora-preamble!", fn_lora_preamble);
}

void lora_definitions() {
    uforth_interpret(": lora-send lora-write drop ;");
    uforth_interpret("defer on-lora");
    uforth_interpret(": _tick-lora lora-available? if pad lora-read on-lora then ;");
    uforth_interpret("' _tick-lora attach-service drop");
}

#else

static void fn_lora_dummy() {
    dpush(1); // Error / not available in LITE_VERSION
}

void lora_bindings() {
    forth_register("lora-init", fn_lora_dummy);
    forth_register("lora-available?", fn_lora_dummy);
    forth_register("lora-read", fn_lora_dummy);
    forth_register("lora-write", fn_lora_dummy);
    forth_register("lora-sf!", fn_lora_dummy);
    forth_register("lora-bw!", fn_lora_dummy);
    forth_register("lora-cr!", fn_lora_dummy);
    forth_register("lora-power!", fn_lora_dummy);
    forth_register("lora-preamble!", fn_lora_dummy);
}

void lora_definitions() {
}

#endif
