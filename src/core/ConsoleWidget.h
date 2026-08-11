#pragma once

#include <Stream.h>
#include <vector>
#include <deque>
#include "display.h"
#include "mykeyboard.h"

#define CONSOLE_MAX_LINES 128
#define CONSOLE_HISTORY_MAX 30

class ConsoleWidget : public Stream {
public:
    ConsoleWidget(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t fontSize = 1);
    ~ConsoleWidget();

    // Stream/Print interface
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;

    // Console operations
    void clear();
    void render();
    
    // Keyboard polling & Line Editing
    // Returns true when a full command line has been submitted.
    // Sets 'exitRequested' to true if the user pressed the exit key.
    bool update(String &line, bool &exitRequested);

private:
    int16_t _x, _y, _w, _h;
    uint8_t _fontSize;
    int16_t _charW, _charH;
    int16_t _cols, _rows;

    // Scrollback buffer (REPL output)
    std::vector<String> _lines;
    std::deque<String> _scrollbackHistory;
    int _scrollOffset;
    bool _scrollMode;

    // Line editing buffer
    String _input;
    int _inputCursor;
    int _inputScroll;
    
    // Command history
    std::deque<String> _history;
    int _historyIdx;
    String _inputSaved;

    // Helper functions
    void drawInput();
    void scrollUp();
    void scrollDown();
    void scrollToBottom();
    void checkInputScroll();
};
