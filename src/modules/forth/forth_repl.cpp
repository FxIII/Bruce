#include "forth_repl.h"
#include "uforth.h"
#include "core/ConsoleWidget.h"
#include "natives/natives.h"
#include <Arduino.h>

struct dict *dict = nullptr;
static struct dict _dictBuf;

extern "C" uforth_stat c_handle(void) {
    return forth_dispatch((CELL)dpop());
}

// Global output redirection callback for uForth C output
static ConsoleWidget* _activeConsole = nullptr;

extern "C" void uforth_print_str(const char *s) {
    if (_activeConsole) {
        _activeConsole->print(s);
    } else {
        Serial.print(s);
    }
}

extern "C" void uforth_select_task(CELL uram);

void forthREPL() {
    // 1. Initialize ConsoleWidget (fullscreen terminal)
    ConsoleWidget widget(0, 0, tftWidth, tftHeight, 1);
    _activeConsole = &widget;

    // 2. Use static dictionary buffer in SRAM
    dict = &_dictBuf;

    // 3. Clean boot
    Serial.println("[Forth] Clean boot");
    memset(dict, 0, sizeof(struct dict));
    dict->version   = DICT_VERSION;
    dict->word_size = sizeof(CELL);
    dict->max_cells = MAX_DICT_CELLS;

    uforth_init();
    uforth_load_prims();

    forth_natives_reset();
    forth_register_all();

    uforth_load_core();

    forth_set_output([](const char *s) {
        Serial.printf("[OUT] '%s' console=%p\n", s, _activeConsole);
        if (_activeConsole) _activeConsole->print(s);
    });
    forth_set_cls([]() { if (_activeConsole) { _activeConsole->clear(); _activeConsole->render(); } });

    // 4. Print welcome greeting
    widget.clear();
    widget.println("uForth 1.2 Console");
    widget.println("Type 'exit' or 'bye' to return.");
    widget.render();

    // 5. Main REPL loop
    String line;
    bool exitRequested = false;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield to other RTOS tasks

        if (widget.update(line, exitRequested)) {
            if (line == "exit" || line == "bye") {
                break;
            }

            // Copy string to buffer and interpret
            char buf[256];
            strncpy(buf, line.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';

            Serial.printf("[REPL] interpret: '%s'\n", buf);
            uforth_stat st = uforth_interpret(buf);

            Serial.printf("[REPL] result: %d\n", (int)st);

            if (st == UFORTH_OK) {
                widget.print(" ok\n");
            } else {
                if (strlen(uforth_abort_details) > 0) {
                    char errbuf[128];
                    snprintf(errbuf, sizeof(errbuf), " ? %s err %d\n", uforth_abort_details, (int)st);
                    widget.print(errbuf);
                } else {
                    char errbuf[32];
                    snprintf(errbuf, sizeof(errbuf), " err %d\n", (int)st);
                    widget.print(errbuf);
                }
                uforth_abort();
            }
            widget.render();
        }

        if (exitRequested) {
            break;
        }
    }

    _activeConsole = nullptr;
    dict = nullptr;
}
