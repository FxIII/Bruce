#include "forth_terminal.h"
#include "core/display.h"
#include <globals.h>

void ForthTerminal::init(uint8_t fontSize, int16_t x, int16_t y, int16_t w, int16_t h) {
    _fontSize = fontSize;
    _x = x; _y = y; _w = w; _h = h;

    _charW = 6 * fontSize;
    _charH = 8 * fontSize;
    _cols  = _w / _charW;
    _rows  = _h / _charH;
    if (_cols < 1) _cols = 1;
    if (_rows < 1) _rows = 1;

    _curRow = 0;
    _curCol = 0;
    _scrollOffset = 0;

    _lines.assign(_rows, std::string(_cols, ' '));
    _history.clear();

    log_d("ForthTerminal init: fontSize=%d cols=%d rows=%d charW=%d charH=%d w=%d h=%d",
          fontSize, _cols, _rows, _charW, _charH, w, h);
}

void ForthTerminal::termScrollUp() {
    // Move oldest line to history
    _history.push_back(_lines[0]);
    if (_history.size() > HISTORY_MAX) _history.pop_front();

    // Shift lines up
    for (int r = 1; r < _rows; r++) _lines[r - 1] = std::move(_lines[r]);
    _lines[_rows - 1].assign(_cols, ' ');

    // Keep view stable if user is scrolled up
    if (_scrollOffset > 0) _scrollOffset++;
}

void ForthTerminal::putChar(char c) {
    if (c < ' ') return;
    if (_curCol >= _cols) {
        _curCol = 0;
        _curRow++;
        if (_curRow >= _rows) {
            termScrollUp();
            _curRow = _rows - 1;
        }
    }
    _lines[_curRow][_curCol] = c;
    _curCol++;
}

void ForthTerminal::cr() {
    _curCol = 0;
    _curRow++;
    if (_curRow >= _rows) {
        termScrollUp();
        _curRow = _rows - 1;
    }
}

void ForthTerminal::print(const char *s) {
    while (*s) {
        if (*s == '\n') cr();
        else putChar(*s);
        s++;
    }
}

void ForthTerminal::clear() {
    _lines.assign(_rows, std::string(_cols, ' '));
    _history.clear();
    _curRow = 0;
    _curCol = 0;
    _scrollOffset = 0;
}

void ForthTerminal::scrollUp() {
    int maxScroll = (int)_history.size();
    if (_scrollOffset < maxScroll) _scrollOffset++;
}

void ForthTerminal::scrollDown() {
    if (_scrollOffset > 0) _scrollOffset--;
}

void ForthTerminal::render() {
    tft.fillRect(_x, _y, _w, _h, bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(_fontSize);

    // Build a virtual view: history + active lines
    int H = (int)_history.size();
    int total = H + _rows;

    // The last visible line index in the virtual array (0-based from top of history)
    int endIdx = total - 1 - _scrollOffset;
    int startIdx = endIdx - (_rows - 1);
    if (startIdx < 0) startIdx = 0;

    for (int i = 0; i < _rows; i++) {
        int idx = startIdx + i;
        if (idx < 0 || idx > endIdx) continue;

        const std::string *line;
        if (idx < H) {
            line = &_history[idx];
        } else {
            int li = idx - H;
            if (li < 0 || li >= _rows) continue;
            line = &_lines[li];
        }

        // Trim trailing spaces before drawing
        size_t end = line->size();
        while (end > 0 && (*line)[end - 1] == ' ') end--;
        if (end > 0) {
            tft.setCursor(_x, _y + i * _charH);
            tft.print(line->substr(0, end).c_str());
        }
    }

    tft.setTextSize(1);
}
