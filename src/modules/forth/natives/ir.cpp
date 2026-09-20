#include "natives_internal.h"
#include "../uforth.h"
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <globals.h>
#include "modules/ir/ir_utils.h"

#define IR_PROTO_RCA 9999

static void fn_ir_tx() {
    uint16_t bits = (uint16_t)dpop();
    int32_t proto = (int32_t)dpop();
    uint64_t data = (uint64_t)dpop();

    if (bruceConfigPins.irTx < 0) {
        dpush(0);
        return;
    }

    setup_ir_pin(bruceConfigPins.irTx, OUTPUT);

    IRsend irsend(bruceConfigPins.irTx);
    irsend.begin();

    bool ok = false;
    if (proto == IR_PROTO_RCA) {
        // RCA / RCA-38 protocol: 4000us header mark/space, 500us/2000us one, 500us/1000us zero
        irsend.sendGeneric(4000, 4000, 500, 2000, 500, 1000, 500, 8000, data, bits, 38000, true, 0, 50);
        if (bruceConfigPins.irTxRepeats > 0) {
            for (uint8_t i = 1; i <= bruceConfigPins.irTxRepeats; i++) {
                irsend.sendGeneric(4000, 4000, 500, 2000, 500, 1000, 500, 8000, data, bits, 38000, true, 0, 50);
            }
        }
        ok = true;
    } else {
        decode_type_t dproto = (decode_type_t)proto;
        ok = irsend.send(dproto, data, bits);
        if (bruceConfigPins.irTxRepeats > 0) {
            for (uint8_t i = 1; i <= bruceConfigPins.irTxRepeats; i++) {
                irsend.send(dproto, data, bits);
            }
        }
    }

    delay(20);
    digitalWrite(bruceConfigPins.irTx, LED_OFF);

    dpush(ok ? 1 : 0);
}

// Helper encoders: all take ( dev subdev func -- data proto bits )

static IRsend _encoder(0);

static void fn_enc_nec() {
    int32_t func = (int32_t)dpop();
    int32_t subdev = (int32_t)dpop();
    int32_t dev = (int32_t)dpop();

    uint16_t addr;
    if (subdev < 0) {
        addr = (uint8_t)dev;
    } else {
        addr = ((uint8_t)subdev << 8) | (uint8_t)dev;
    }
    uint32_t data = _encoder.encodeNEC(addr, (uint16_t)func);
    dpush((DCELL)data);
    dpush((DCELL)decode_type_t::NEC);
    dpush(32);
}

static void fn_enc_rca() {
    int32_t func = (int32_t)dpop();
    dpop(); // subdev unused
    int32_t dev = (int32_t)dpop();

    uint8_t d = (uint8_t)(dev & 0x0F);
    uint8_t f = (uint8_t)(func & 0xFF);
    uint32_t data = ((uint32_t)d << 20) |
                    ((uint32_t)f << 12) |
                    ((uint32_t)(~d & 0x0F) << 8) |
                    ((uint32_t)(~f & 0xFF));
    dpush((DCELL)data);
    dpush((DCELL)IR_PROTO_RCA);
    dpush(24);
}

static void fn_enc_sony() {
    int32_t func = (int32_t)dpop();
    int32_t subdev = (int32_t)dpop();
    int32_t dev = (int32_t)dpop();

    uint16_t bits = 12;
    uint32_t data = 0;
    if (subdev < 0) {
        bits = 12;
        data = _encoder.encodeSony(12, (uint16_t)func, (uint16_t)dev);
    } else if (subdev > 0) {
        bits = 20;
        data = _encoder.encodeSony(20, (uint16_t)func, (uint16_t)dev, (uint16_t)subdev);
    } else {
        bits = 15;
        data = _encoder.encodeSony(15, (uint16_t)func, (uint16_t)dev);
    }
    dpush((DCELL)data);
    dpush((DCELL)decode_type_t::SONY);
    dpush(bits);
}

static void fn_enc_rc5() {
    int32_t func = (int32_t)dpop();
    dpop(); // subdev unused
    int32_t dev = (int32_t)dpop();

    uint16_t data = _encoder.encodeRC5((uint8_t)dev, (uint8_t)func);
    dpush((DCELL)data);
    dpush((DCELL)decode_type_t::RC5);
    dpush(13);
}

static void fn_enc_rc6() {
    int32_t func = (int32_t)dpop();
    dpop(); // subdev unused
    int32_t dev = (int32_t)dpop();

    uint64_t data = _encoder.encodeRC6((uint32_t)dev, (uint8_t)func);
    dpush((DCELL)data);
    dpush((DCELL)decode_type_t::RC6);
    dpush(20);
}

static void fn_enc_samsung() {
    int32_t func = (int32_t)dpop();
    dpop(); // subdev unused
    int32_t dev = (int32_t)dpop();

    uint32_t data = _encoder.encodeSAMSUNG((uint8_t)dev, (uint8_t)func);
    dpush((DCELL)data);
    dpush((DCELL)decode_type_t::SAMSUNG);
    dpush(32);
}

static void fn_enc_panasonic() {
    int32_t func = (int32_t)dpop();
    int32_t subdev = (int32_t)dpop();
    int32_t dev = (int32_t)dpop();

    uint8_t sub = (subdev < 0) ? 0 : (uint8_t)subdev;
    uint64_t data = _encoder.encodePanasonic(0x4004, (uint8_t)dev, sub, (uint8_t)func);
    dpush((DCELL)data);
    dpush((DCELL)decode_type_t::PANASONIC);
    dpush(48);
}

static void fn_enc_sharp() {
    int32_t func = (int32_t)dpop();
    dpop(); // subdev unused
    int32_t dev = (int32_t)dpop();

    uint32_t data = _encoder.encodeSharp((uint16_t)dev, (uint16_t)func);
    dpush((DCELL)data);
    dpush((DCELL)decode_type_t::SHARP);
    dpush(15);
}

static void fn_enc_jvc() {
    int32_t func = (int32_t)dpop();
    dpop(); // subdev unused
    int32_t dev = (int32_t)dpop();

    uint16_t data = _encoder.encodeJVC((uint8_t)dev, (uint8_t)func);
    dpush((DCELL)data);
    dpush((DCELL)decode_type_t::JVC);
    dpush(16);
}

static void fn_enc_denon() {
    int32_t func = (int32_t)dpop();
    dpop(); // subdev unused
    int32_t dev = (int32_t)dpop();

    uint32_t data = _encoder.encodeSharp((uint16_t)dev, (uint16_t)func);
    dpush((DCELL)data);
    dpush((DCELL)decode_type_t::DENON);
    dpush(15);
}

void ir_bindings() {
    forth_register("ir-tx", fn_ir_tx);             // ir-tx        ( data proto bits -- ok? )

    // Top 10 Encoders: all take ( dev subdev func -- data proto bits )
    forth_register("ir-nec", fn_enc_nec);          // ir-nec       ( dev subdev func -- data proto bits )
    forth_register("ir-rca", fn_enc_rca);          // ir-rca       ( dev subdev func -- data proto bits )
    forth_register("ir-sony", fn_enc_sony);        // ir-sony      ( dev subdev func -- data proto bits )
    forth_register("ir-rc5", fn_enc_rc5);          // ir-rc5       ( dev subdev func -- data proto bits )
    forth_register("ir-rc6", fn_enc_rc6);          // ir-rc6       ( dev subdev func -- data proto bits )
    forth_register("ir-samsung", fn_enc_samsung);  // ir-samsung   ( dev subdev func -- data proto bits )
    forth_register("ir-panasonic", fn_enc_panasonic);// ir-panasonic( dev subdev func -- data proto bits )
    forth_register("ir-sharp", fn_enc_sharp);      // ir-sharp     ( dev subdev func -- data proto bits )
    forth_register("ir-jvc", fn_enc_jvc);          // ir-jvc       ( dev subdev func -- data proto bits )
    forth_register("ir-denon", fn_enc_denon);      // ir-denon     ( dev subdev func -- data proto bits )
}

void ir_definitions() {
    char buf[64];

    // Export protocol constants directly from decode_type_t enum
    snprintf(buf, sizeof(buf), "%d constant IR-UNKNOWN", (int)decode_type_t::UNKNOWN);
    uforth_interpret(buf);
    snprintf(buf, sizeof(buf), "%d constant IR-NEC", (int)decode_type_t::NEC);
    uforth_interpret(buf);
    snprintf(buf, sizeof(buf), "%d constant IR-SONY", (int)decode_type_t::SONY);
    uforth_interpret(buf);
    snprintf(buf, sizeof(buf), "%d constant IR-RC5", (int)decode_type_t::RC5);
    uforth_interpret(buf);
    snprintf(buf, sizeof(buf), "%d constant IR-RC6", (int)decode_type_t::RC6);
    uforth_interpret(buf);
    snprintf(buf, sizeof(buf), "%d constant IR-SAMSUNG", (int)decode_type_t::SAMSUNG);
    uforth_interpret(buf);
    snprintf(buf, sizeof(buf), "%d constant IR-PANASONIC", (int)decode_type_t::PANASONIC);
    uforth_interpret(buf);
    snprintf(buf, sizeof(buf), "%d constant IR-LG", (int)decode_type_t::LG);
    uforth_interpret(buf);
    snprintf(buf, sizeof(buf), "%d constant IR-JVC", (int)decode_type_t::JVC);
    uforth_interpret(buf);
    snprintf(buf, sizeof(buf), "%d constant IR-DENON", (int)decode_type_t::DENON);
    uforth_interpret(buf);
    snprintf(buf, sizeof(buf), "%d constant IR-SHARP", (int)decode_type_t::SHARP);
    uforth_interpret(buf);
    snprintf(buf, sizeof(buf), "%d constant IR-RCA", (int)IR_PROTO_RCA);
    uforth_interpret(buf);
}
