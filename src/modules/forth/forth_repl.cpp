#include "forth_repl.h"
#include "forth_terminal.h"
#include "uforth.h"
#include "natives/natives.h"

#include <Arduino.h>
#include <globals.h>
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"

// ─── Dictionary allocation ────────────────────────────────────────────────────

struct dict *dict = nullptr;

// ─── Display layout ──────────────────────────────────────────────────────────

#define FONT_SIZE   1   // output area font size
#define INPUT_FONT  2   // input line font size
#define INPUT_H   (8 * INPUT_FONT + 4)  // font height + padding

static ForthTerminal _term;

// Current input line
static String _input;
static int    _inputScroll = 0;

// Source log: accumulates `: word ... ;` lines so `store` can replay them
#define MAX_SOURCE_LOG 64
static String _sourceLog[MAX_SOURCE_LOG];
static int    _sourceLogCount = 0;

// ─── Output helpers ───────────────────────────────────────────────────────────

// Write to terminal buffer only — caller is responsible for calling render()
static void _appendOutput(const char *s) {
    _term.print(s);
}

// ─── Input line ──────────────────────────────────────────────────────────────

static void _inputScrollToEnd() {
    int avail = tftWidth / (6 * INPUT_FONT) - 1;
    _inputScroll = max(0, (int)_input.length() - avail);
}

static void _drawInput() {
    int cols   = tftWidth / (6 * INPUT_FONT);
    int inputY = tftHeight - INPUT_H;
    int avail  = cols - 1;

    int maxScroll = max(0, (int)_input.length() - avail);
    if (_inputScroll > maxScroll) _inputScroll = maxScroll;
    if (_inputScroll < 0)         _inputScroll = 0;

    char prompt = (_inputScroll > 0) ? '$' : '>';
    String visible = _input.substring(_inputScroll, _inputScroll + avail);
    while ((int)visible.length() < avail) visible += ' ';

    tft.setTextSize(INPUT_FONT);
    tft.setTextColor(bruceConfig.bgColor, bruceConfig.priColor);
    tft.setCursor(0, inputY);
    tft.print(String(prompt) + visible);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(1);
}

// ─── c_handle — called by uForth `cf` primitive ───────────────────────────────

// ─── Native function implementations ─────────────────────────────────────────

static void _fn_emit() {
    char c = (char)dpop();
    char s[2] = {c, '\0'};
    _appendOutput(s);
}

static void _fn_cr() {
    _appendOutput("\n");
}

static void _fn_dot() {
    char buf[24];
    DCELL n = dpop();
    snprintf(buf, sizeof(buf), "%lld ", (long long)n);
    _appendOutput(buf);
}

static void _fn_cls() {
    _term.clear();
    _term.render();
}

static void _fn_words() {
    _appendOutput("\n");
    CELL idx = dict->last_word_idx;
    while (idx) {
        uint8_t flags = (uint8_t)uforth_dict[idx + 1];
        uint8_t len   = flags & 0x3F;
        if (len > 0 && len < 63) {
            char name[64];
            memcpy(name, (char *)(uforth_dict + idx + 2), len);
            name[len] = '\0';
            _appendOutput(name);
            _appendOutput(" ");
        }
        idx = uforth_dict[idx];
    }
    _appendOutput("\n");
}

// ─── c_handle — called by uForth `cf` primitive ───────────────────────────────

extern "C" uforth_stat c_handle(void) {
    return forth_dispatch((CELL)dpop());
}

// ─── Core I/O words ───────────────────────────────────────────────────────────

static void _loadCorePrims() {
    forth_register("emit",  _fn_emit);
    forth_register("cr",    _fn_cr);
    forth_register(".",     _fn_dot);
    forth_register("cls",   _fn_cls);
    forth_register("words", _fn_words);
}

// ─── Persistence ─────────────────────────────────────────────────────────────

static FS *_pickFS() {
    if (SD.begin()) return &SD;
    return &LittleFS;
}

static void _loadInitFs() {
    FS *fs = _pickFS();
    File f = fs->open("/forth/init.fs", FILE_READ);
    if (!f) return;

    String line;
    while (f.available()) {
        char c = (char)f.read();
        if (c == '\n') {
            line.trim();
            if (line.length() > 0) {
                char buf[TIB_SIZE];
                strncpy(buf, line.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                uforth_interpret(buf);
            }
            line = "";
        } else {
            line += c;
        }
    }
    line.trim();
    if (line.length() > 0) {
        char buf[TIB_SIZE];
        strncpy(buf, line.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        uforth_interpret(buf);
    }
    f.close();
}

// `store [prefix]` — write matching source-log entries to /forth/init.fs.
static void _storeWords(const String &prefix) {
    FS *fs = _pickFS();
    fs->mkdir("/forth");

    String existing = "";
    File r = fs->open("/forth/init.fs", FILE_READ);
    if (r) {
        while (r.available()) existing += (char)r.read();
        r.close();
    }

    File w = fs->open("/forth/init.fs", FILE_WRITE);
    if (!w) { _appendOutput("store: write failed\n"); return; }

    int start = 0;
    while (start < (int)existing.length()) {
        int nl = existing.indexOf('\n', start);
        if (nl < 0) nl = existing.length();
        String line = existing.substring(start, nl);
        line.trim();
        start = nl + 1;
        if (line.length() == 0) continue;
        if (prefix.length() > 0 && line.startsWith(": ")) {
            int sp = line.indexOf(' ', 2);
            String wname = (sp > 2) ? line.substring(2, sp) : line.substring(2);
            if (wname.startsWith(prefix)) continue;
        }
        w.println(line);
    }

    for (int i = 0; i < _sourceLogCount; i++) {
        String entry = _sourceLog[i];
        int sp = entry.indexOf(' ', 2);
        String wname = (sp > 2) ? entry.substring(2, sp) : entry.substring(2);
        if (prefix.length() == 0 || wname.startsWith(prefix)) {
            w.println(entry);
        }
    }

    w.close();
    _appendOutput("saved /forth/init.fs\n");
}

// ─── Main REPL ────────────────────────────────────────────────────────────────

static struct dict _dictBuf;

void forthREPL() {
    // Reset state on each entry
    _input = "";
    _inputScroll = 0;
    _sourceLogCount = 0;

    // Allocate dict once, reinitialize on each entry
    if (dict == nullptr) {
        struct dict *d = (struct dict *)ps_malloc(sizeof(struct dict));
        dict = d ? d : &_dictBuf;
    }
    memset(dict, 0, sizeof(struct dict));
    dict->version   = DICT_VERSION;
    dict->word_size = sizeof(CELL);
    dict->max_cells = MAX_DICT_CELLS;

    tft.fillScreen(bruceConfig.bgColor);
    _term.init(FONT_SIZE, 0, 0, tftWidth, tftHeight - INPUT_H);

    uforth_init();
    uforth_load_prims();
    forth_define_words();
    forth_natives_reset();
    _loadCorePrims();
    forth_register_all();
    _loadInitFs();

    _appendOutput("uForth 1.2  type 'bye' to exit\n");
    _term.render();
    _drawInput();
    delay(300);

    while (true) {
        keyStroke ks = _getKeyPress();

        if (!ks.pressed) { delay(20); continue; }

        if (ks.exit_key && !ks.enter) break;

        // Arrow keys: scroll output
        if (!ks.word.empty()) {
            char c = ks.word[0];
            if (c == (char)0xDA) { // up
                _term.scrollUp();
                _term.render();
                _drawInput();
                continue;
            }
            if (c == (char)0xD9) { // down
                _term.scrollDown();
                _term.render();
                _drawInput();
                continue;
            }
            if (c == (char)0xD8) { // left
                _inputScroll--;
                _drawInput();
                continue;
            }
            if (c == (char)0xD7) { // right
                _inputScroll++;
                _drawInput();
                continue;
            }
        }

        if (ks.enter) {
            String line = _input;
            _input = "";
            _inputScroll = 0;

            _appendOutput("> ");
            _appendOutput(line.c_str());
            _appendOutput("\n");

            if (line == "bye" || line == "exit") {
                _drawInput();
                break;
            }

            if (line.startsWith("store")) {
                String prefix = "";
                if (line.length() > 6) { prefix = line.substring(6); prefix.trim(); }
                _storeWords(prefix);
            } else {
                char buf[TIB_SIZE];
                strncpy(buf, line.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                uforth_stat st = uforth_interpret(buf);
                if (st == UFORTH_OK) {
                    _appendOutput(" ok\n");
                    if (line.startsWith(":") && _sourceLogCount < MAX_SOURCE_LOG)
                        _sourceLog[_sourceLogCount++] = line;
                } else {
                    char errbuf[24];
                    snprintf(errbuf, sizeof(errbuf), " err %d\n", (int)st);
                    _appendOutput(errbuf);
                    uforth_abort();
                }
            }
            _term.render();
            _drawInput();
            continue;
        }

        if (ks.del) {
            if (_input.length() > 0) {
                _input.remove(_input.length() - 1);
                _inputScrollToEnd();
                _drawInput();
            }
            continue;
        }

        if (!ks.word.empty()) {
            for (char c : ks.word) {
                if (isPrintable(c)) _input += c;
            }
            _inputScrollToEnd();
            _drawInput();
        }
    }
}
