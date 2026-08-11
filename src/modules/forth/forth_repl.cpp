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

static bool loadForthSession(const char* filepath, FS *fs) {
    File file = fs->open(filepath, "r");
    if (!file) return false;

    size_t bytesRead = file.read((uint8_t*)dict, sizeof(struct dict));
    if (bytesRead != sizeof(struct dict)) {
        file.close();
        return false;
    }

    bytesRead = file.read((uint8_t*)uforth_ram, TOTAL_RAM_CELLS * sizeof(DCELL));
    if (bytesRead != TOTAL_RAM_CELLS * sizeof(DCELL)) {
        file.close();
        return false;
    }
    file.close();

    // Restore engine pointers
    uforth_dict = (CELL*)dict;
    uforth_iram = (struct uforth_iram*) uforth_ram;
    uforth_select_task(uforth_iram->curtask_idx);

    // Re-register native functions deterministically without duplicating dictionary words
    forth_set_restore_mode(true);
    forth_natives_reset();
    forth_register_all();
    forth_set_restore_mode(false);

    log_d("Forth session loaded successfully from %s", filepath);
    return true;
}

static bool saveForthSession(const char* filepath, FS *fs) {
    if (!fs->exists("/forth")) {
        fs->mkdir("/forth");
    }

    File file = fs->open(filepath, "w");
    if (!file) return false;

    size_t bytesWritten = file.write((const uint8_t*)dict, sizeof(struct dict));
    if (bytesWritten != sizeof(struct dict)) {
        file.close();
        return false;
    }

    bytesWritten = file.write((const uint8_t*)uforth_ram, TOTAL_RAM_CELLS * sizeof(DCELL));
    if (bytesWritten != TOTAL_RAM_CELLS * sizeof(DCELL)) {
        file.close();
        return false;
    }
    file.close();

    log_d("Forth session saved successfully to %s", filepath);
    return true;
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

    // 3. Mount filesystem & load session
    FS *fs = nullptr;
    bool hasStorage = getFsStorage(fs);
    bool sessionLoaded = false;

    if (hasStorage && fs->exists("/forth/session.bin")) {
        sessionLoaded = loadForthSession("/forth/session.bin", fs);
    }

    if (!sessionLoaded) {
        log_d("No Forth session found, initializing clean VM");
        memset(dict, 0, sizeof(struct dict));
        dict->version   = DICT_VERSION;
        dict->word_size = sizeof(CELL);
        dict->max_cells = MAX_DICT_CELLS;

        uforth_init();
        uforth_load_prims();

        forth_natives_reset();
        forth_register_all();
    }

    forth_set_output([](const char *s) { if (_activeConsole) _activeConsole->print(s); });
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

            uforth_interpret(buf);
            widget.render();
        }

        if (exitRequested) {
            break;
        }
    }

    // 6. Save session and cleanup
    if (hasStorage) {
        saveForthSession("/forth/session.bin", fs);
    }

    _activeConsole = nullptr;
    if (dict != &_dictBuf) {
        free(dict);
        dict = nullptr;
    }
}
