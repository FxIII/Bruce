#include "forth_repl.h"
#include "uforth.h"
#include "core/ConsoleWidget.h"
#include "natives/natives.h"
#include <Arduino.h>

struct dict *dict = nullptr;
static struct dict _dictBuf;
static bool _consoleDirty = false;

extern "C" uforth_stat c_handle(void) {
    return forth_dispatch((CELL)dpop());
}

// Global output redirection callback for uForth C output
static ConsoleWidget* _activeConsole = nullptr;

ConsoleWidget* getActiveConsoleWidget() {
    return _activeConsole;
}

extern "C" void uforth_print_str(const char *s) {
    if (_activeConsole) {
        _activeConsole->print(s);
        _consoleDirty = true;
    } else {
        Serial.print(s);
    }
}

extern "C" void uforth_select_task(CELL uram);

bool forth_repl_step_once(ConsoleWidget* widget) {
    if (!widget) return false;
    String line;
    bool exitRequested = false;

    if (widget->update(line, exitRequested)) {
        // Level 3: Wipe history file and exit
        if (line == "bye!!" || line == "exit!!") {
            widget->clearHistoryFile("/forth/history.txt");
            return true;
        }

        // Level 2: Exit without saving current session history
        if (line == "bye!" || line == "exit!") {
            return true;
        }

        // Level 1: Save history and exit
        if (line == "bye" || line == "exit") {
            widget->saveHistory("/forth/history.txt");
            return true;
        }

        // Copy string to buffer and interpret
        char buf[256];
        strncpy(buf, line.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        uforth_stat st = uforth_interpret(buf);

        if (st == UFORTH_OK) {
            widget->print(" ok\n");
        } else {
            if (strlen(uforth_abort_details) > 0) {
                char errbuf[128];
                snprintf(errbuf, sizeof(errbuf), " ? %s err %d\n", uforth_abort_details, (int)st);
                widget->print(errbuf);
            } else {
                char errbuf[32];
                snprintf(errbuf, sizeof(errbuf), " err %d\n", (int)st);
                widget->print(errbuf);
            }
            uforth_abort();
        }
        widget->render();
        _consoleDirty = false;
    } else if (_consoleDirty) {
        widget->render();
        _consoleDirty = false;
    }

    if (exitRequested) {
        widget->saveHistory("/forth/history.txt");
        return true;
    }

    return false;
}

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
    forth_natives_register_bindings();

    uforth_load_core();
    forth_natives_register_definitions();

    forth_set_output([](const char *s) {
        if (_activeConsole) {
            _activeConsole->print(s);
            _consoleDirty = true;
        }
    });
    forth_set_cls([]() {
        if (_activeConsole) {
            _activeConsole->clear();
            _activeConsole->render();
            _consoleDirty = false;
        }
    });

    // 4. Load persistent command history and print welcome greeting
    widget.loadHistory("/forth/history.txt");

    widget.clear();
    widget.println("uForth 1.2 Console");
    widget.println("Type 'bye' (save), 'bye!' (no save), 'bye!!' (wipe)");
    widget.render();
    _consoleDirty = false;

    // 5. Main REPL loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield to other RTOS tasks

        if (forth_repl_step_once(&widget)) {
            break;
        }
    }

    _activeConsole = nullptr;
    dict = nullptr;
}
