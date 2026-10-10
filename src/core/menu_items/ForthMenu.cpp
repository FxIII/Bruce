#include "ForthMenu.h"
#include "core/display.h"
#include "core/utils.h"
#include "modules/forth/forth_repl.h"

void ForthMenu::optionsMenu() {
    forthREPL();
}

void ForthMenu::drawIcon(float scale) {
    clearIconArea();

    int iconW = scale * 64;
    int iconH = scale * 52;

    if (iconW % 2 != 0) iconW++;
    if (iconH % 2 != 0) iconH++;

    int cx = iconCenterX;
    int cy = iconCenterY;

    int stroke = max(2, (int)(scale * 2.5f));
    int radius = max(2, (int)(scale * 3.0f));
    int layerH = scale * 12;
    int pad    = scale * 4;

    int totalH = 3 * layerH + 2 * pad;
    int startY = cy - totalH / 2;

    auto drawLayer = [&](int y, int w) {
        int x = cx - w / 2;
        tft.fillRoundRect(x, y, w, layerH, radius, bruceConfig.priColor);
        if (w > 2 * stroke && layerH > 2 * stroke) {
            tft.fillRoundRect(x + stroke, y + stroke, w - 2 * stroke, layerH - 2 * stroke, max(1, radius - 1), bruceConfig.bgColor);
        }
    };

    // Draw 3 layers of the Forth data stack (top, middle, bottom)
    int topY = startY;
    int midY = startY + layerH + pad;
    int botY = startY + 2 * (layerH + pad);

    drawLayer(topY, iconW);
    drawLayer(midY, iconW);
    drawLayer(botY, iconW);

    // Draw a bold solid cursor block inside the top layer (TOS)
    int cursorW = scale * 10;
    int cursorH = layerH - 2 * stroke - max(2, (int)(scale * 2.0f));
    if (cursorH < 2) cursorH = 2;
    int cursorX = cx - cursorW / 2;
    int cursorY = topY + (layerH - cursorH) / 2;
    tft.fillRect(cursorX, cursorY, cursorW, cursorH, bruceConfig.priColor);
}
