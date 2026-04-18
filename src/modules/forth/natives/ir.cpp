#include "ir.h"
#include "natives.h"
#include "modules/ir/custom_ir.h"

static void fn_ir_nec() {
    DCELL command = dpop();
    DCELL address = dpop();
    char addr_str[9], cmd_str[9];
    snprintf(addr_str, sizeof(addr_str), "%08lX", (uint32_t)address);
    snprintf(cmd_str,  sizeof(cmd_str),  "%08lX", (uint32_t)command);
    sendNECCommand(String(addr_str), String(cmd_str), true);
}

static void fn_ir_necext() {
    DCELL command = dpop();
    DCELL address = dpop();
    char addr_str[9], cmd_str[9];
    snprintf(addr_str, sizeof(addr_str), "%08lX", (uint32_t)address);
    snprintf(cmd_str,  sizeof(cmd_str),  "%08lX", (uint32_t)command);
    sendNECextCommand(String(addr_str), String(cmd_str), true);
}

static void fn_ir_rc5() {
    DCELL command = dpop();
    DCELL address = dpop();
    char addr_str[9], cmd_str[9];
    snprintf(addr_str, sizeof(addr_str), "%08lX", (uint32_t)address);
    snprintf(cmd_str,  sizeof(cmd_str),  "%08lX", (uint32_t)command);
    sendRC5Command(String(addr_str), String(cmd_str), true);
}

static void fn_ir_rc6() {
    DCELL command = dpop();
    DCELL address = dpop();
    char addr_str[9], cmd_str[9];
    snprintf(addr_str, sizeof(addr_str), "%08lX", (uint32_t)address);
    snprintf(cmd_str,  sizeof(cmd_str),  "%08lX", (uint32_t)command);
    sendRC6Command(String(addr_str), String(cmd_str), true);
}

static void fn_ir_samsung() {
    DCELL command = dpop();
    DCELL address = dpop();
    char addr_str[9], cmd_str[9];
    snprintf(addr_str, sizeof(addr_str), "%08lX", (uint32_t)address);
    snprintf(cmd_str,  sizeof(cmd_str),  "%08lX", (uint32_t)command);
    sendSamsungCommand(String(addr_str), String(cmd_str), true);
}

static void fn_ir_sirc() {
    DCELL command = dpop();
    DCELL address = dpop();
    char addr_str[9], cmd_str[9];
    snprintf(addr_str, sizeof(addr_str), "%08lX", (uint32_t)address);
    snprintf(cmd_str,  sizeof(cmd_str),  "%08lX", (uint32_t)command);
    sendSonyCommand(String(addr_str), String(cmd_str), 12, true);
}

static void fn_ir_sirc15() {
    DCELL command = dpop();
    DCELL address = dpop();
    char addr_str[9], cmd_str[9];
    snprintf(addr_str, sizeof(addr_str), "%08lX", (uint32_t)address);
    snprintf(cmd_str,  sizeof(cmd_str),  "%08lX", (uint32_t)command);
    sendSonyCommand(String(addr_str), String(cmd_str), 15, true);
}

static void fn_ir_sirc20() {
    DCELL command = dpop();
    DCELL address = dpop();
    char addr_str[9], cmd_str[9];
    snprintf(addr_str, sizeof(addr_str), "%08lX", (uint32_t)address);
    snprintf(cmd_str,  sizeof(cmd_str),  "%08lX", (uint32_t)command);
    sendSonyCommand(String(addr_str), String(cmd_str), 20, true);
}

static void fn_ir_kaseikyo() {
    DCELL command = dpop();
    DCELL address = dpop();
    char addr_str[9], cmd_str[9];
    snprintf(addr_str, sizeof(addr_str), "%08lX", (uint32_t)address);
    snprintf(cmd_str,  sizeof(cmd_str),  "%08lX", (uint32_t)command);
    sendKaseikyoCommand(String(addr_str), String(cmd_str), true);
}

void forth_register_ir() {
    forth_register("br.ir.nec",      fn_ir_nec);
    forth_register("br.ir.necext",   fn_ir_necext);
    forth_register("br.ir.rc5",      fn_ir_rc5);
    forth_register("br.ir.rc6",      fn_ir_rc6);
    forth_register("br.ir.samsung",  fn_ir_samsung);
    forth_register("br.ir.sirc",     fn_ir_sirc);
    forth_register("br.ir.sirc15",   fn_ir_sirc15);
    forth_register("br.ir.sirc20",   fn_ir_sirc20);
    forth_register("br.ir.kaseikyo", fn_ir_kaseikyo);
}
