#pragma once

#include <Arduino.h>
#include <deque>
#include <string>

class ForthTerminal {
public:
    void init(uint8_t fontSize, int16_t x, int16_t y, int16_t w, int16_t h);
    void putChar(char c);
    void cr();
    void print(const char *s);
    void clear();
    void scrollUp();
    void scrollDown();
    void scrollToBottom();
    void render();

private:
    void termScrollUp();

    uint8_t  _fontSize = 1;
    int16_t  _x = 0, _y = 0, _w = 0, _h = 0;
    int      _cols = 0, _rows = 0;
    int      _charW = 6, _charH = 8;
    int      _curRow = 0, _curCol = 0;
    int      _scrollOffset = 0;  // 0 = bottom, >0 = scrolled up

    static const size_t HISTORY_MAX = 100;

    std::vector<std::string> _lines;    // active screen buffer (rows x cols)
    std::deque<std::string>  _history;  // lines scrolled off the top
};
