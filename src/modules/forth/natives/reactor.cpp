#include "natives_internal.h"
#include "../uforth.h"
#include "../forth_repl.h"
#include "core/mykeyboard.h"
#include <Arduino.h>

#define MAX_SERVICES 16
#define MAX_TIMERS 8

static CELL _services[MAX_SERVICES + 1]; // 1-based index (1..16)

struct ReactorTimer {
    bool active;
    bool periodic;
    uint32_t interval_ms;
    uint32_t last_trigger_ms;
    CELL xt;
};

static ReactorTimer _timers[MAX_TIMERS + 1]; // 1-based index (1..8)

static void exec_xt(CELL xt) {
    if (xt == 0) return;
    exec(xt, false, uforth_uram->ridx - 1);
}

void forth_reactor_step(void) {
    uint32_t now = millis();

    // 1. Step all attached service ticks
    for (int i = 1; i <= MAX_SERVICES; i++) {
        if (_services[i] != 0) {
            exec_xt(_services[i]);
        }
    }

    // 2. Step all active timers
    for (int i = 1; i <= MAX_TIMERS; i++) {
        if (_timers[i].active) {
            if (now - _timers[i].last_trigger_ms >= _timers[i].interval_ms) {
                _timers[i].last_trigger_ms = now;
                CELL xt = _timers[i].xt;
                if (!_timers[i].periodic) {
                    _timers[i].active = false;
                }
                exec_xt(xt);
            }
        }
    }
}

static void fn_reactor_step() {
    forth_reactor_step();
}

static void fn_repl_step() {
    ConsoleWidget* widget = getActiveConsoleWidget();
    bool shouldExit = forth_repl_step_once(widget);
    dpush(shouldExit ? 1 : 0);
}

static void fn_attach_service() {
    CELL xt = (CELL)dpop();
    for (int i = 1; i <= MAX_SERVICES; i++) {
        if (_services[i] == 0) {
            _services[i] = xt;
            dpush(i);
            return;
        }
    }
    dpush(0); // Error: no free service slots
}

static void fn_detach_service() {
    CELL slot = (CELL)dpop();
    if (slot >= 1 && slot <= MAX_SERVICES) {
        _services[slot] = 0;
    }
}

static void fn_dot_services() {
    forth_output("\n--- Active Reactor Services ---\n");
    int count = 0;
    for (int i = 1; i <= MAX_SERVICES; i++) {
        if (_services[i] != 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), " Slot %2d: xt=%d\n", i, (int)_services[i]);
            forth_output(buf);
            count++;
        }
    }
    if (count == 0) {
        forth_output(" (none)\n");
    }
    forth_output("-------------------------------\n");
}

static void fn_every() {
    CELL xt = (CELL)dpop();
    DCELL ms = dpop();
    for (int i = 1; i <= MAX_TIMERS; i++) {
        if (!_timers[i].active) {
            _timers[i].active = true;
            _timers[i].periodic = true;
            _timers[i].interval_ms = (uint32_t)(ms > 0 ? ms : 1000);
            _timers[i].last_trigger_ms = millis();
            _timers[i].xt = xt;
            dpush(i);
            return;
        }
    }
    dpush(0); // Error: no free timer slots
}

static void fn_after() {
    CELL xt = (CELL)dpop();
    DCELL ms = dpop();
    for (int i = 1; i <= MAX_TIMERS; i++) {
        if (!_timers[i].active) {
            _timers[i].active = true;
            _timers[i].periodic = false;
            _timers[i].interval_ms = (uint32_t)(ms > 0 ? ms : 1000);
            _timers[i].last_trigger_ms = millis();
            _timers[i].xt = xt;
            dpush(i);
            return;
        }
    }
    dpush(0);
}

static void fn_cancel_timer() {
    CELL slot = (CELL)dpop();
    if (slot >= 1 && slot <= MAX_TIMERS) {
        _timers[slot].active = false;
    }
}

void reactor_bindings() {
    // Reset services and timers
    for (int i = 0; i <= MAX_SERVICES; i++) _services[i] = 0;
    for (int i = 0; i <= MAX_TIMERS; i++) _timers[i].active = false;

    forth_register("attach-service", fn_attach_service);
    forth_register("detach-service", fn_detach_service);
    forth_register(".services", fn_dot_services);
    forth_register("every", fn_every);
    forth_register("after", fn_after);
    forth_register("cancel-timer", fn_cancel_timer);
    forth_register("reactor-step", fn_reactor_step);
    forth_register("repl-step", fn_repl_step);
}

void reactor_definitions() {
    uforth_interpret(": react begin reactor-step repl-step 10 ms until ;");
}
