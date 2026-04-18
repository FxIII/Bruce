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

void forth_register_ir(const char *prefix) {
    char name[64];
    snprintf(name, sizeof(name), "%s.nec",      prefix); forth_register(name, fn_ir_nec);
    snprintf(name, sizeof(name), "%s.necext",   prefix); forth_register(name, fn_ir_necext);
    snprintf(name, sizeof(name), "%s.rc5",      prefix); forth_register(name, fn_ir_rc5);
    snprintf(name, sizeof(name), "%s.rc6",      prefix); forth_register(name, fn_ir_rc6);
    snprintf(name, sizeof(name), "%s.samsung",  prefix); forth_register(name, fn_ir_samsung);
    snprintf(name, sizeof(name), "%s.sirc",     prefix); forth_register(name, fn_ir_sirc);
    snprintf(name, sizeof(name), "%s.sirc15",   prefix); forth_register(name, fn_ir_sirc15);
    snprintf(name, sizeof(name), "%s.sirc20",   prefix); forth_register(name, fn_ir_sirc20);
    snprintf(name, sizeof(name), "%s.kaseikyo", prefix); forth_register(name, fn_ir_kaseikyo);
}
