#include "forth_repl.h"
#include "uforth.h"

#include <Arduino.h>
#include <globals.h>
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"

// ─── Dictionary allocation ───────────────────────────────────────────────────

// uforth.h declares `extern struct dict *dict;` (with C linkage via extern "C").
// We provide the definition here.
struct dict *dict = nullptr;

// ─── Display layout ──────────────────────────────────────────────────────────

#define FONT_W   6
#define FONT_H   8
#define MARGIN_X 2
#define MARGIN_Y 2

static int _cols;
static int _rows;  // output rows (total rows minus 1 for input)

// Ring-buffer of output lines for redraw
#define MAX_LINES 32
static String _lines[MAX_LINES];
static int    _lineHead  = 0;
static int    _lineCount = 0;

// Partial output accumulator (flushed on '\n' or when full)
static String _curOut;

// Current input line
static String _input;

// Source log: accumulates `: word ... ;` lines so `store` can replay them
#define MAX_SOURCE_LOG 64
static String _sourceLog[MAX_SOURCE_LOG];
static int    _sourceLogCount = 0;

// ─── Output helpers ───────────────────────────────────────────────────────────

static void _flushCurOut() {
    _lines[_lineHead] = _curOut;
    _lineHead = (_lineHead + 1) % MAX_LINES;
    if (_lineCount < MAX_LINES) _lineCount++;
    _curOut = "";
}

static void _appendOutput(const char *s) {
    while (*s) {
        if (*s == '\n') {
            _flushCurOut();
        } else {
            _curOut += *s;
            if ((int)_curOut.length() >= _cols) {
                _flushCurOut();
            }
        }
        s++;
    }
}

// ─── Screen redraw ────────────────────────────────────────────────────────────

static void _redraw() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(1);

    int total    = (_lineCount < _rows) ? _lineCount : _rows;
    int startIdx = (_lineHead - total + MAX_LINES) % MAX_LINES;
    for (int i = 0; i < total; i++) {
        int lineIdx = (startIdx + i) % MAX_LINES;
        tft.setCursor(MARGIN_X, MARGIN_Y + i * FONT_H);
        tft.print(_lines[lineIdx]);
    }

    // Input line at the bottom, inverted colours
    tft.setTextColor(bruceConfig.bgColor, bruceConfig.priColor);
    int inputY = MARGIN_Y + _rows * FONT_H;
    String row = "> " + _input;
    while ((int)row.length() < _cols) row += ' ';
    row = row.substring(0, _cols);
    tft.setCursor(MARGIN_X, inputY);
    tft.print(row);

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
}

// ─── c_handle — called by uForth `cf` primitive ───────────────────────────────

extern "C" uforth_stat c_handle(void) {
    DCELL cmd = dpop();
    char  buf[24];

    switch (cmd) {
    case 1: { // emit  ( c -- )
        char c = (char)dpop();
        char s[2] = {c, '\0'};
        _appendOutput(s);
        break;
    }
    case 2: // cr  ( -- )
        _flushCurOut();
        break;

    case 3: { // .  ( n -- )  print number + space
        DCELL n = dpop();
        snprintf(buf, sizeof(buf), "%lld ", (long long)n);
        _appendOutput(buf);
        break;
    }
    case 4: // cls  ( -- )
        _lineHead = 0; _lineCount = 0; _curOut = "";
        break;

    case 5: { // words  ( -- )  print all word names
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
        _flushCurOut();
        break;
    }
    default:
        return E_NOT_A_WORD;
    }
    return UFORTH_OK;
}

// ─── Core I/O words ───────────────────────────────────────────────────────────

static void _loadCorePrims() {
    uforth_interpret(": emit  1 cf ;");
    uforth_interpret(": cr    2 cf ;");
    uforth_interpret(": .     3 cf ;");
    uforth_interpret(": cls   4 cf ;");
    uforth_interpret(": words 5 cf ;");
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
    // Last line without newline
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
// Words are stored as the original source text that defined them.
static void _storeWords(const String &prefix) {
    FS *fs = _pickFS();
    fs->mkdir("/forth");

    // We rewrite the whole file: start from existing init.fs, skip lines
    // whose word name matches the prefix (they'll be re-written fresh from
    // the source log), then append the source-log entries for the prefix.

    // Read current file
    String existing = "";
    File r = fs->open("/forth/init.fs", FILE_READ);
    if (r) {
        while (r.available()) existing += (char)r.read();
        r.close();
    }

    // Write merged result
    File w = fs->open("/forth/init.fs", FILE_WRITE);
    if (!w) { _appendOutput("store: write failed\n"); return; }

    // Copy existing lines that don't conflict with what we're about to save
    int start = 0;
    while (start < (int)existing.length()) {
        int nl = existing.indexOf('\n', start);
        if (nl < 0) nl = existing.length();
        String line = existing.substring(start, nl);
        line.trim();
        start = nl + 1;

        if (line.length() == 0) continue;

        // If prefix is given, skip lines whose word name starts with prefix
        if (prefix.length() > 0 && line.startsWith(": ")) {
            int sp = line.indexOf(' ', 2);
            String wname = (sp > 2) ? line.substring(2, sp) : line.substring(2);
            if (wname.startsWith(prefix)) continue; // will be re-added from log
        }
        w.println(line);
    }

    // Append from source log
    for (int i = 0; i < _sourceLogCount; i++) {
        if (prefix.length() == 0 || _sourceLog[i].indexOf(": " + prefix) == 0
                || _sourceLog[i].startsWith(": " + prefix)) {
            // Extract word name to check prefix properly
            String entry = _sourceLog[i];
            int sp = entry.indexOf(' ', 2);
            String wname = (sp > 2) ? entry.substring(2, sp) : entry.substring(2);
            if (prefix.length() == 0 || wname.startsWith(prefix)) {
                w.println(entry);
            }
        }
    }

    w.close();
    _appendOutput("saved /forth/init.fs\n");
}

// ─── Main REPL ────────────────────────────────────────────────────────────────

static bool _forthInited = false;
static struct dict _dictBuf;  // static to avoid heap fragmentation on small builds;
                               // use ps_malloc on psram-capable builds

void forthREPL() {
    if (!_forthInited) {
        // Try PSRAM first, fall back to static buffer
        struct dict *d = (struct dict *)ps_malloc(sizeof(struct dict));
        if (d) {
            dict = d;
        } else {
            memset(&_dictBuf, 0, sizeof(_dictBuf));
            dict = &_dictBuf;
        }

        dict->version   = DICT_VERSION;
        dict->word_size = sizeof(CELL);
        dict->max_cells = MAX_DICT_CELLS;

        uforth_init();
        uforth_load_prims();
        _loadCorePrims();
        _loadInitFs();

        _forthInited = true;
    }

    _cols = (tftWidth  - 2 * MARGIN_X) / FONT_W;
    _rows = (tftHeight - 2 * MARGIN_Y) / FONT_H - 1;
    if (_rows < 2) _rows = 2;

    _appendOutput("uForth 1.2  type 'bye' to exit\n");
    _redraw();

    while (true) {
        keyStroke ks = _getKeyPress();

        if (!ks.pressed) { delay(20); continue; }

        if (ks.exit_key) break;

        if (ks.enter) {
            String line = _input;
            _input = "";

            _appendOutput("> ");
            _appendOutput(line.c_str());
            _appendOutput("\n");

            if (line == "bye" || line == "exit") {
                _redraw();
                break;
            }

            if (line.startsWith("store")) {
                String prefix = "";
                if (line.length() > 6) {
                    prefix = line.substring(6);
                    prefix.trim();
                }
                _storeWords(prefix);
            } else {
                char buf[TIB_SIZE];
                strncpy(buf, line.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                uforth_stat st = uforth_interpret(buf);
                if (st == UFORTH_OK) {
                    _appendOutput(" ok\n");
                    // Log word definitions for `store`
                    if (line.startsWith(":") && _sourceLogCount < MAX_SOURCE_LOG) {
                        _sourceLog[_sourceLogCount++] = line;
                    }
                } else {
                    char errbuf[24];
                    snprintf(errbuf, sizeof(errbuf), " err %d\n", (int)st);
                    _appendOutput(errbuf);
                    uforth_abort();
                }
            }
            _redraw();
            continue;
        }

        if (ks.del) {
            if (_input.length() > 0) {
                _input.remove(_input.length() - 1);
                _redraw();
            }
            continue;
        }

        if (!ks.word.empty()) {
            for (char c : ks.word) {
                if (isPrintable(c)) _input += c;
            }
            _redraw();
        }
    }
}
