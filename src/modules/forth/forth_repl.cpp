#include "forth_repl.h"
#include "uforth.h"
#include "core/ConsoleWidget.h"
#include <Arduino.h>

struct dict *dict = nullptr;
static struct dict _dictBuf;

extern "C" uforth_stat c_handle(void) {
    return UFORTH_OK;
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

void forthREPL() {
    // 1. Initialize ConsoleWidget (fullscreen terminal)
    ConsoleWidget widget(0, 0, tftWidth, tftHeight, 1);
    _activeConsole = &widget;

    // 2. Allocate uForth dictionary in PSRAM
    if (dict == nullptr) {
        struct dict *d = (struct dict *)ps_malloc(sizeof(struct dict));
        dict = d ? d : &_dictBuf;
    }
    memset(dict, 0, sizeof(struct dict));
    dict->version   = DICT_VERSION;
    dict->word_size = sizeof(CELL);
    dict->max_cells = MAX_DICT_CELLS;

    // 3. Initialize uForth engine
    uforth_init();
    uforth_load_prims();

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

            uforth_interpret(buf);
            widget.render();
        }

        if (exitRequested) {
            break;
        }
    }

    // 6. Cleanup
    _activeConsole = nullptr;
    if (dict != &_dictBuf) {
        free(dict);
        dict = nullptr;
    }
}
