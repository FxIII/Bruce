#include "natives_internal.h"
#include "../uforth.h"
#include "../forth_repl.h"
#include "core/mykeyboard.h"
#include <Arduino.h>
#include <stdlib.h>

#define MAX_SERVICES 16
#define MAX_TIMERS 8

static DCELL _services_ram_idx = 0;
static DCELL _timers_ram_idx = 0;

static void fn_set_services_addr() {
    _services_ram_idx = dpop();
    for (int i = 0; i < MAX_SERVICES; i++) {
        uforth_ram[_services_ram_idx + i] = 0;
    }
}

static void fn_set_timers_addr() {
    _timers_ram_idx = dpop();
    for (int i = 0; i < MAX_TIMERS * 2; i++) {
        uforth_ram[_timers_ram_idx + i] = 0;
    }
}

static void fn_repl_step() {
    ConsoleWidget* widget = getActiveConsoleWidget();
    bool shouldExit = forth_repl_step_once(widget);
    dpush(shouldExit ? 1 : 0);
}

static void fn_attach_service() {
    CELL xt = (CELL)dpop();
    if (_services_ram_idx == 0 || xt == 0) {
        dpush(0);
        return;
    }
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (uforth_ram[_services_ram_idx + i] == 0) {
            uforth_ram[_services_ram_idx + i] = xt;
            dpush(i + 1); // 1-based slot index
            return;
        }
    }
    dpush(0); // Error: no free service slots
}

static void fn_detach_service() {
    CELL slot = (CELL)dpop();
    if (_services_ram_idx != 0 && slot >= 1 && slot <= MAX_SERVICES) {
        uforth_ram[_services_ram_idx + (slot - 1)] = 0;
    }
}

static void fn_dot_services() {
    forth_output("\n--- Active Reactor Services ---\n");
    int count = 0;
    if (_services_ram_idx != 0) {
        for (int i = 0; i < MAX_SERVICES; i++) {
            DCELL xt = uforth_ram[_services_ram_idx + i];
            if (xt != 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), " Slot %2d: xt=%d\n", i + 1, (int)xt);
                forth_output(buf);
                count++;
            }
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
    if (_timers_ram_idx == 0 || xt == 0) {
        dpush(0);
        return;
    }
    for (int i = 0; i < MAX_TIMERS; i++) {
        DCELL xt_cell = uforth_ram[_timers_ram_idx + i * 2];
        if (xt_cell == 0) {
            uforth_ram[_timers_ram_idx + i * 2] = (DCELL)abs((int)xt); // Positive for periodic
            uint32_t interval = (uint32_t)(ms > 0 ? ms : 1000);
            uint32_t now = millis();
            uforth_ram[_timers_ram_idx + i * 2 + 1] = (((uint64_t)now) << 32) | (interval & 0xFFFFFFFFULL);
            dpush(i + 1); // 1-based slot index
            return;
        }
    }
    dpush(0); // Error: no free timer slots
}

static void fn_after() {
    CELL xt = (CELL)dpop();
    DCELL ms = dpop();
    if (_timers_ram_idx == 0 || xt == 0) {
        dpush(0);
        return;
    }
    for (int i = 0; i < MAX_TIMERS; i++) {
        DCELL xt_cell = uforth_ram[_timers_ram_idx + i * 2];
        if (xt_cell == 0) {
            uforth_ram[_timers_ram_idx + i * 2] = -(DCELL)abs((int)xt); // Negative for one-shot
            uint32_t interval = (uint32_t)(ms > 0 ? ms : 1000);
            uint32_t now = millis();
            uforth_ram[_timers_ram_idx + i * 2 + 1] = (((uint64_t)now) << 32) | (interval & 0xFFFFFFFFULL);
            dpush(i + 1);
            return;
        }
    }
    dpush(0);
}

static void fn_cancel_timer() {
    CELL slot = (CELL)dpop();
    if (_timers_ram_idx != 0 && slot >= 1 && slot <= MAX_TIMERS) {
        uforth_ram[_timers_ram_idx + (slot - 1) * 2] = 0;
    }
}

static void fn_timer_remaining() {
    CELL slot = (CELL)dpop();
    if (_timers_ram_idx != 0 && slot >= 1 && slot <= MAX_TIMERS) {
        DCELL xt_cell = uforth_ram[_timers_ram_idx + (slot - 1) * 2];
        if (xt_cell != 0) {
            DCELL time_data = uforth_ram[_timers_ram_idx + (slot - 1) * 2 + 1];
            uint32_t last_ms = (uint32_t)(time_data >> 32);
            uint32_t interval = (uint32_t)(time_data & 0xFFFFFFFFULL);
            uint32_t elapsed = millis() - last_ms;
            uint32_t rem = (elapsed >= interval) ? 0 : (interval - elapsed);
            dpush(rem);
            return;
        }
    }
    dpush(0);
}

static void fn_timer_active() {
    CELL slot = (CELL)dpop();
    if (_timers_ram_idx != 0 && slot >= 1 && slot <= MAX_TIMERS) {
        DCELL xt_cell = uforth_ram[_timers_ram_idx + (slot - 1) * 2];
        dpush(xt_cell != 0 ? 1 : 0);
    } else {
        dpush(0);
    }
}

static void fn_dot_timers() {
    forth_output("\n--- Active Timers ---\n");
    int count = 0;
    uint32_t now = millis();
    if (_timers_ram_idx != 0) {
        for (int i = 0; i < MAX_TIMERS; i++) {
            DCELL xt_cell = uforth_ram[_timers_ram_idx + i * 2];
            if (xt_cell != 0) {
                DCELL time_data = uforth_ram[_timers_ram_idx + i * 2 + 1];
                uint32_t last_ms = (uint32_t)(time_data >> 32);
                uint32_t interval = (uint32_t)(time_data & 0xFFFFFFFFULL);
                uint32_t elapsed = now - last_ms;
                uint32_t rem = (elapsed >= interval) ? 0 : (interval - elapsed);
                bool periodic = (xt_cell > 0);
                CELL xt = (CELL)abs((long long)xt_cell);
                
                char buf[80];
                snprintf(buf, sizeof(buf), "%d: %s [%u ms]@%u ms -> %d\n",
                         i + 1,
                         periodic ? "Per" : "One",
                         (unsigned int)interval,
                         (unsigned int)rem,
                         (int)xt);
                forth_output(buf);
                count++;
            }
        }
    }
    if (count == 0) {
        forth_output(" (none)\n");
    }
    forth_output("---------------------\n");
}

static void fn_reactor_check() {
    int count = 0;
    uint32_t now = millis();

    // 1. Scan services (pushed first -> bottom of stack)
    if (_services_ram_idx != 0) {
        for (int i = 0; i < MAX_SERVICES; i++) {
            DCELL xt = uforth_ram[_services_ram_idx + i];
            if (xt != 0) {
                dpush(xt);
                count++;
            }
        }
    }

    // 2. Scan timers (pushed second -> top of stack, executed first by LIFO pop)
    if (_timers_ram_idx != 0) {
        for (int i = 0; i < MAX_TIMERS; i++) {
            DCELL xt_cell = uforth_ram[_timers_ram_idx + i * 2];
            if (xt_cell != 0) {
                DCELL time_data = uforth_ram[_timers_ram_idx + i * 2 + 1];
                uint32_t last_ms = (uint32_t)(time_data >> 32);
                uint32_t interval = (uint32_t)(time_data & 0xFFFFFFFFULL);
                if (now - last_ms >= interval) {
                    CELL xt = (CELL)abs((long long)xt_cell);
                    if (xt_cell < 0) {
                        // One-shot timer -> deactivate
                        uforth_ram[_timers_ram_idx + i * 2] = 0;
                    } else {
                        // Periodic timer -> update last_trigger_ms
                        uforth_ram[_timers_ram_idx + i * 2 + 1] = (((uint64_t)now) << 32) | (interval & 0xFFFFFFFFULL);
                    }
                    dpush(xt);
                    count++;
                }
            }
        }
    }

    // 3. Push total count of ready XTs
    dpush(count);
}

void forth_reactor_step(void) {
    uforth_interpret("reactor-step");
}

void reactor_bindings() {
    _services_ram_idx = 0;
    _timers_ram_idx = 0;

    forth_register("reactor-set-services-addr", fn_set_services_addr);
    forth_register("reactor-set-timers-addr", fn_set_timers_addr);

    forth_register("attach-service", fn_attach_service);
    forth_register("detach-service", fn_detach_service);
    forth_register(".services", fn_dot_services);

    forth_register("every", fn_every);
    forth_register("after", fn_after);
    forth_register("cancel-timer", fn_cancel_timer);
    forth_register("timer-remaining", fn_timer_remaining);
    forth_register("timer-active?", fn_timer_active);
    forth_register(".timers", fn_dot_timers);

    forth_register("reactor-check", fn_reactor_check);
    forth_register("repl-step", fn_repl_step);
}

void reactor_definitions() {
    uforth_interpret("variable services 15 allot");
    uforth_interpret("variable timers 15 allot");
    uforth_interpret("services reactor-set-services-addr");
    uforth_interpret("timers reactor-set-timers-addr");
    uforth_interpret(": reset-reactor 16 0 do 0 services i + ! loop 16 0 do 0 timers i + ! loop ;");
    uforth_interpret(": exec-xts dup? if 0 do exec loop then ;");
    uforth_interpret(": reactor-step reactor-check exec-xts ;");
    uforth_interpret(": react begin reactor-step repl-step 10 ms until ;");
}
