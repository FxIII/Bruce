#include "forth_repl.h"
#include "forth_terminal.h"
#include "uforth.h"
#include "natives/natives.h"

#include <Arduino.h>
#include <deque>
#include <string>
#include <globals.h>
#include "core/display.h"
#include "core/mykeyboard.h"

// ─── Dictionary allocation ────────────────────────────────────────────────────

struct dict *dict = nullptr;

// ─── Display layout ──────────────────────────────────────────────────────────

#define FONT_SIZE   1   // output area font size
#define INPUT_FONT  2   // input line font size
#define INPUT_H   (8 * INPUT_FONT + 4)  // font height + padding

static ForthTerminal _term;

// Current input line
static String _input;
static int    _inputCursor   = 0;  // insert position (0..length)
static int    _inputScroll   = 0;  // first visible char index
static int    _nInputLines   = 1;  // 1 or 2, tracks current terminal split
static bool   _scrollMode    = false;  // opt toggle: hide input, arrows scroll output

// Input history
#define HISTORY_MAX 16
static std::deque<std::string> _history;   // front = most recent
static int                     _historyIdx = -1;  // -1 = not browsing; 0 = most recent
static String                  _inputSaved;        // line saved when entering history

// ─── Output helpers ───────────────────────────────────────────────────────────

// Write to terminal buffer only — caller is responsible for calling render()
static void _appendOutput(const char *s) {
    _term.print(s);
}

// ─── Input line ──────────────────────────────────────────────────────────────

static int _inputAvail()  { return tftWidth / (6 * INPUT_FONT) - 1; }
static int _inputNeededLines() { return (_input.length() > (size_t)_inputAvail()) ? 2 : 1; }

// Adjust scroll so cursor stays visible across 1 or 2 lines
static void _scrollToCursor() {
    int avail  = _inputAvail();
    int window = avail * _inputNeededLines();
    if (_inputCursor < _inputScroll)           _inputScroll = _inputCursor;
    if (_inputCursor >= _inputScroll + window) _inputScroll = _inputCursor - window + 1;
    if (_inputScroll < 0)                      _inputScroll = 0;
}

// Load a history entry into _input (0 = most recent)
static void _historyLoad(int idx) {
    _input = String(_history[idx].c_str());
    _inputCursor = _input.length();
    _scrollToCursor();
}

// Push a line into history (front = most recent, max HISTORY_MAX)
static void _historyPush(const String &line) {
    if (line.length() == 0) return;
    _history.push_front(std::string(line.c_str()));
    if (_history.size() > HISTORY_MAX) _history.pop_back();
}

static void _drawInput() {
    int avail  = _inputAvail();
    int nLines = _inputNeededLines();
    int totalH = INPUT_H * nLines;
    int inputY = tftHeight - totalH;

    // Clamp scroll
    int window    = avail * nLines;
    int maxScroll = max(0, (int)_input.length() - window);
    if (_inputScroll > maxScroll) _inputScroll = maxScroll;
    if (_inputScroll < 0)         _inputScroll = 0;


    // If line count changed, re-render terminal to clear/restore the affected area
    if (nLines != _nInputLines) {
        _nInputLines = nLines;
        _term.render();
    }

    // Fill padding pixels below each line (INPUT_H - text height = 4px)
    int pad = INPUT_H - 8 * INPUT_FONT;
    for (int l = 0; l < nLines; l++)
        tft.fillRect(0, inputY + l * INPUT_H + 8 * INPUT_FONT, tftWidth, pad, bruceConfig.priColor);

    tft.setTextSize(INPUT_FONT);
    tft.setTextColor(bruceConfig.bgColor, bruceConfig.priColor);

    // Line 1
    char prompt1 = (_inputScroll > 0) ? '$' : '>';
    String vis1  = _input.substring(_inputScroll, _inputScroll + avail);
    while ((int)vis1.length() < avail) vis1 += ' ';
    tft.setCursor(0, inputY);
    tft.print(String(prompt1) + vis1);

    // Line 2 (if needed)
    if (nLines == 2) {
        int start2    = _inputScroll + avail;
        bool overflow = ((int)_input.length() > start2 + avail);
        char prompt2  = overflow ? '$' : ' ';
        String vis2   = _input.substring(start2, start2 + avail);
        while ((int)vis2.length() < avail) vis2 += ' ';
        tft.setCursor(0, inputY + INPUT_H);
        tft.print(String(prompt2) + vis2);
    }

    // Cursor: 1px vertical line
    int charW  = 6 * INPUT_FONT;
    int relPos = _inputCursor - _inputScroll;
    int cLine, cCol;
    if (relPos < avail) { cLine = 0; cCol = 1 + relPos; }
    else                { cLine = 1; cCol = 1 + (relPos - avail); }
    tft.drawFastVLine(cCol * charW, inputY + cLine * INPUT_H + 1, INPUT_H - 2, bruceConfig.bgColor);

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

// ─── Main REPL ────────────────────────────────────────────────────────────────

static struct dict _dictBuf;

void forthREPL() {
    // Reset state on each entry
    _input = "";
    _inputCursor = 0;
    _inputScroll = 0;
    _nInputLines = 1;
    _scrollMode  = false;
    _historyIdx  = -1;

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
    forth_set_output(_appendOutput);
    _loadCorePrims();
    forth_register_all();

    _appendOutput("uForth 1.2  type 'bye' to exit\n");
    _term.render();
    _drawInput();
    delay(300);

    while (true) {
        keyStroke ks = _getKeyPress();

        if (!ks.pressed) { continue; }

        if (ks.exit_key && !ks.enter) break;

        // opt (gui) → toggle scroll mode
        if (ks.gui) {
            _scrollMode = !_scrollMode;
            if (_scrollMode) {
                // hide input area
                int totalH = INPUT_H * _nInputLines;
                tft.fillRect(0, tftHeight - totalH, tftWidth, totalH, bruceConfig.bgColor);
            } else {
                _drawInput();
            }
            continue;
        }

        if (_scrollMode) {
            if (!ks.word.empty()) {
                char c = ks.word[0];
                if (c == (char)0xDA || c == ';') { _term.scrollUp();   _term.render(); }
                if (c == (char)0xD9 || c == '.') { _term.scrollDown(); _term.render(); }
            }
            continue;
        }

        // Arrow keys
        if (!ks.word.empty()) {
            char c = ks.word[0];
            if (c == (char)0xDA) { // up → history back
                if (!_history.empty()) {
                    if (_historyIdx == -1) { _inputSaved = _input; _historyIdx = 0; }
                    else if (_historyIdx < (int)_history.size() - 1) _historyIdx++;
                    _historyLoad(_historyIdx);
                    _drawInput();
                }
                continue;
            }
            if (c == (char)0xD9) { // down → history forward
                if (_historyIdx >= 0) {
                    if (_historyIdx == 0) {
                        _historyIdx = -1;
                        _input = _inputSaved;
                        _inputCursor = _input.length();
                        _scrollToCursor();
                    } else {
                        _historyIdx--;
                        _historyLoad(_historyIdx);
                    }
                    _drawInput();
                }
                continue;
            }
            if (c == (char)0xD8) { // left → move cursor
                if (_inputCursor > 0) { _inputCursor--; _scrollToCursor(); _drawInput(); }
                continue;
            }
            if (c == (char)0xD7) { // right → move cursor
                if (_inputCursor < (int)_input.length()) { _inputCursor++; _scrollToCursor(); _drawInput(); }
                continue;
            }
        }

        if (ks.enter) {
            String line = _input;
            _historyPush(line);
            _historyIdx = -1;
            _input = "";
            _inputCursor = 0;
            _inputScroll = 0;

            _appendOutput("> ");
            _appendOutput(line.c_str());
            _appendOutput("\n");

            if (line == "bye" || line == "exit") {
                _drawInput();
                break;
            }

            {
                char buf[TIB_SIZE];
                strncpy(buf, line.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                uforth_stat st = uforth_interpret(buf);
                if (st == UFORTH_OK) {
                    _appendOutput(" ok\n");
                } else {
                    char errbuf[24];
                    snprintf(errbuf, sizeof(errbuf), " err %d\n", (int)st);
                    _appendOutput(errbuf);
                    uforth_abort();
                }
            }
            _term.scrollToBottom();
            _term.render();
            _drawInput();
            continue;
        }

        if (ks.del) {
            if (_inputCursor > 0) {
                _input.remove(_inputCursor - 1, 1);
                _inputCursor--;
                _scrollToCursor();
                _drawInput();
            }
            continue;
        }

        if (!ks.word.empty()) {
            for (char c : ks.word) {
                if (isPrintable(c)) {
                    _input = _input.substring(0, _inputCursor) + c + _input.substring(_inputCursor);
                    _inputCursor++;
                }
            }
            _scrollToCursor();
            _drawInput();
        }
    }
}
