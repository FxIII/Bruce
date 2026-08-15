#include "forth_repl.h"
#include "tbforth.h"
#include "tbforth_core.h"
#include "core/ConsoleWidget.h"
#include "natives/natives.h"
#include <Arduino.h>

#define FORTH_SESSION_ENABLED 0

struct dict *dict = nullptr;
static struct dict _dictBuf;

extern "C" tbforth_stat c_handle(void) {
    return forth_dispatch((CELL)dpop());
}

static ConsoleWidget* _activeConsole = nullptr;

extern "C" void tbforth_print_str(const char *s) {
    if (_activeConsole) {
        _activeConsole->print(s);
    } else {
        Serial.print(s);
    }
}

void forthREPL() {
    // 1. Initialize ConsoleWidget (fullscreen terminal)
    ConsoleWidget widget(0, 0, tftWidth, tftHeight, 1);
    _activeConsole = &widget;

    // 2. Allocate ToolboxForth dictionary in PSRAM
    if (dict == nullptr) {
        struct dict *d = (struct dict *)ps_malloc(sizeof(struct dict));
        dict = d ? d : &_dictBuf;
    }

    // 3. Clean boot
    Serial.println("[Forth] ToolboxForth 4.08 Clean boot");
    memset(dict, 0, sizeof(struct dict));
    dict->version   = DICT_VERSION;
    dict->word_size = sizeof(CELL);
    dict->max_cells = MAX_DICT_CELLS;

    tbforth_init();
    tbforth_load_prims();

    forth_natives_reset();
    forth_register_all();

    tbforth_load_core();

    forth_set_output([](const char *s) {
        Serial.printf("[OUT] '%s' console=%p\n", s, _activeConsole);
        if (_activeConsole) _activeConsole->print(s);
    });
    forth_set_cls([]() { if (_activeConsole) { _activeConsole->clear(); _activeConsole->render(); } });

    // 4. Print welcome greeting
    widget.clear();
    widget.println("ToolboxForth 4.08 Console");
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
            tbforth_stat st = tbforth_interpret(buf);

            Serial.printf("[REPL] result: %d\n", (int)st);

            if (st == U_OK) {
                widget.print(" ok\n");
            } else {
                char errbuf[32];
                snprintf(errbuf, sizeof(errbuf), " err %d\n", (int)st);
                widget.print(errbuf);
                tbforth_abort(0);
            }
            widget.render();
        }

        if (exitRequested) {
            break;
        }
    }

    _activeConsole = nullptr;
    if (dict != &_dictBuf) {
        free(dict);
        dict = nullptr;
    }
}
