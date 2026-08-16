#include "ConsoleWidget.h"
#include "sd_functions.h"
#include <Arduino.h>
#include <FS.h>
#include <globals.h>

ConsoleWidget::ConsoleWidget(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t fontSize) {
    _x = x; _y = y; _w = w; _h = h;
    _fontSize = fontSize;
    _charW = 6 * fontSize;
    _charH = 8 * fontSize;
    _cols = _w / _charW;
    _rows = _h / _charH;
    if (_cols < 1) _cols = 1;
    if (_rows < 1) _rows = 1;

    clear();
}

ConsoleWidget::~ConsoleWidget() {
}

void ConsoleWidget::clear() {
    _lines.clear();
    _lines.push_back("");
    _scrollbackHistory.clear();
    _scrollOffset = 0;
    _scrollMode = false;
    _input = "";
    _inputCursor = 0;
    _inputScroll = 0;
    _historyIdx = -1;
}

size_t ConsoleWidget::write(uint8_t c) {
    if (_lines.empty()) _lines.push_back("");

    if (c == '\n') {
        _lines.push_back("");
        if ((int)_lines.size() > _rows - 1) {
            _scrollbackHistory.push_back(_lines[0]);
            if (_scrollbackHistory.size() > CONSOLE_MAX_LINES) {
                _scrollbackHistory.pop_front();
            }
            _lines.erase(_lines.begin());
        }
    } else if (c >= ' ') {
        // Word wrapping: split if it exceeds column width
        if ((int)_lines.back().length() >= _cols) {
            _lines.push_back("");
            if ((int)_lines.size() > _rows - 1) {
                _scrollbackHistory.push_back(_lines[0]);
                if (_scrollbackHistory.size() > CONSOLE_MAX_LINES) {
                    _scrollbackHistory.pop_front();
                }
                _lines.erase(_lines.begin());
            }
        }
        _lines.back() += (char)c;
    }
    return 1;
}

size_t ConsoleWidget::write(const uint8_t *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        write(buffer[i]);
    }
    return size;
}

int ConsoleWidget::available() { return 0; }
int ConsoleWidget::read() { return -1; }
int ConsoleWidget::peek() { return -1; }
void ConsoleWidget::flush() {}

void ConsoleWidget::scrollUp() {
    int maxScroll = (int)_scrollbackHistory.size();
    if (_scrollOffset < maxScroll) {
        _scrollOffset++;
    }
}

void ConsoleWidget::scrollDown() {
    if (_scrollOffset > 0) {
        _scrollOffset--;
    }
}

void ConsoleWidget::scrollToBottom() {
    _scrollOffset = 0;
}

void ConsoleWidget::render() {
    // Clear widget area
    tft.fillRect(_x, _y, _w, _h, bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(_fontSize);

    int H = (int)_scrollbackHistory.size();
    int activeRows = _rows - 1; // Reserved last row for input prompt
    int totalLines = H + (int)_lines.size();

    // Calculate vertical viewing window
    int endIdx = totalLines - 1 - _scrollOffset;
    int startIdx = endIdx - (activeRows - 1);
    if (startIdx < 0) startIdx = 0;

    for (int i = 0; i < activeRows; i++) {
        int idx = startIdx + i;
        if (idx < 0 || idx > endIdx) continue;

        String lineStr;
        if (idx < H) {
            lineStr = _scrollbackHistory[idx];
        } else {
            int li = idx - H;
            if (li < 0 || li >= (int)_lines.size()) continue;
            lineStr = _lines[li];
        }

        // Draw line
        tft.setCursor(_x, _y + i * _charH);
        tft.print(lineStr.c_str());
    }

    // Draw input line at bottom (if not in scroll mode)
    if (!_scrollMode) {
        drawInput();
    } else {
        // Draw scroll mode indicator
        tft.setTextColor(bruceConfig.bgColor, bruceConfig.priColor);
        tft.setCursor(_x, _y + (_rows - 1) * _charH);
        tft.print(" -- SCROLL MODE -- ");
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    }
    tft.setTextSize(1);
}

void ConsoleWidget::drawInput() {
    int inputY = _y + (_rows - 1) * _charH;
    // Clear input line area
    tft.fillRect(_x, inputY, _w, _charH, bruceConfig.bgColor);
    
    tft.setCursor(_x, inputY);
    tft.print("> ");

    // Draw input text starting from _inputScroll
    int visibleLen = _cols - 2; // Subtract prompt "> "
    String visibleText = _input.substring(_inputScroll, _inputScroll + visibleLen);
    tft.print(visibleText.c_str());

    // Draw blinking cursor
    int cursorCol = _inputCursor - _inputScroll;
    if (cursorCol >= 0 && cursorCol <= visibleLen) {
        int cursorX = _x + (2 + cursorCol) * _charW;
        tft.fillRect(cursorX, inputY + _charH - 2, _charW, 2, bruceConfig.priColor);
    }
}

void ConsoleWidget::checkInputScroll() {
    int visibleLen = _cols - 2;
    if (_inputCursor < _inputScroll) {
        _inputScroll = _inputCursor;
    } else if (_inputCursor > _inputScroll + visibleLen) {
        _inputScroll = _inputCursor - visibleLen;
    }
}

bool ConsoleWidget::update(String &line, bool &exitRequested) {
    keyStroke ks = _getKeyPress();
    if (!ks.pressed) return false;

    // Handle exit
    if (ks.exit_key && !ks.enter) {
        exitRequested = true;
        return false;
    }

    // Handle toggle scroll mode via Option / GUI key
    if (ks.gui) {
        _scrollMode = !_scrollMode;
        if (!_scrollMode) {
            scrollToBottom();
        }
        render();
        return false;
    }

    if (_scrollMode) {
        // In scroll mode, arrow keys scroll up/down
        if (!ks.word.empty()) {
            char c = ks.word[0];
            if (c == (char)0xDA || c == ';') { scrollUp();   render(); }
            if (c == (char)0xD9 || c == '.') { scrollDown(); render(); }
        }
        return false;
    }

    // In normal edit mode:
    if (!ks.word.empty()) {
        char c = ks.word[0];
        // Up arrow: history backward
        if (c == (char)0xDA) {
            if (!_history.empty()) {
                if (_historyIdx == -1) {
                    _inputSaved = _input;
                    _historyIdx = 0;
                } else if (_historyIdx < (int)_history.size() - 1) {
                    _historyIdx++;
                }
                _input = _history[_history.size() - 1 - _historyIdx];
                _inputCursor = _input.length();
                checkInputScroll();
                drawInput();
            }
            return false;
        }
        // Down arrow: history forward
        if (c == (char)0xD9) {
            if (_historyIdx >= 0) {
                if (_historyIdx == 0) {
                    _historyIdx = -1;
                    _input = _inputSaved;
                    _inputCursor = _input.length();
                } else {
                    _historyIdx--;
                    _input = _history[_history.size() - 1 - _historyIdx];
                    _inputCursor = _input.length();
                }
                checkInputScroll();
                drawInput();
            }
            return false;
        }
        // Left arrow: move cursor left
        if (c == (char)0xD8) {
            if (_inputCursor > 0) {
                _inputCursor--;
                checkInputScroll();
                drawInput();
            }
            return false;
        }
        // Right arrow: move cursor right
        if (c == (char)0xD7) {
            if (_inputCursor < (int)_input.length()) {
                _inputCursor++;
                checkInputScroll();
                drawInput();
            }
            return false;
        }
    }

    // Handle delete/backspace
    if (ks.del) {
        if (_inputCursor > 0) {
            _input.remove(_inputCursor - 1, 1);
            _inputCursor--;
            checkInputScroll();
            drawInput();
        }
        return false;
    }

    // Handle enter key submit
    if (ks.enter) {
        line = _input;
        // Push to history
        if (!line.isEmpty()) {
            _history.push_back(line);
            if (_history.size() > CONSOLE_HISTORY_MAX) {
                _history.pop_front();
            }
        }
        _historyIdx = -1;
        _input = "";
        _inputCursor = 0;
        _inputScroll = 0;

        // Echo the line to scrollback
        print("> ");
        println(line.c_str());

        render();
        return true;
    }

    // Handle text input characters
    if (!ks.word.empty()) {
        for (char c : ks.word) {
            if (c >= ' ' && c <= '~') {
                _input = _input.substring(0, _inputCursor) + c + _input.substring(_inputCursor);
                _inputCursor++;
                checkInputScroll();
            }
        }
        drawInput();
    }

    return false;
}

void ConsoleWidget::loadHistory(const String &filepath) {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs->exists(filepath)) return;

    File file = fs->open(filepath, "r");
    if (!file) return;

    _history.clear();
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (!line.isEmpty()) {
            _history.push_back(line);
            if (_history.size() > CONSOLE_HISTORY_MAX) {
                _history.pop_front();
            }
        }
    }
    file.close();
    _historyIdx = -1;
}

void ConsoleWidget::saveHistory(const String &filepath) {
    FS *fs = nullptr;
    if (!getFsStorage(fs)) return;

    if (filepath.startsWith("/forth/") && !fs->exists("/forth")) {
        fs->mkdir("/forth");
    }

    File file = fs->open(filepath, "w");
    if (!file) return;

    for (const auto &cmd : _history) {
        file.println(cmd);
    }
    file.close();
}

void ConsoleWidget::clearHistoryFile(const String &filepath) {
    _history.clear();
    _historyIdx = -1;

    FS *fs = nullptr;
    if (getFsStorage(fs) && fs->exists(filepath)) {
        fs->remove(filepath);
    }
}
