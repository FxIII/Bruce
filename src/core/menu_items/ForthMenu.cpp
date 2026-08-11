#include "ForthMenu.h"
#include "core/display.h"
#include "core/utils.h"
#include "modules/forth/forth_repl.h"

void ForthMenu::optionsMenu() {
    forthREPL();
}

void ForthMenu::drawIcon(float scale) {
    clearIconArea();

    int iconW = scale * 40;
    int iconH = scale * 40;

    if (iconW % 2 != 0) iconW++;
    if (iconH % 2 != 0) iconH++;

    // Draw a 3-layer stack icon representing Forth
    int cx = iconCenterX;
    int cy = iconCenterY;
    int layerH = iconH / 4;
    int layerW = iconW;
    int pad = scale * 4;

    // Bottom Layer
    tft.drawRect(cx - layerW / 2, cy + layerH / 2 + pad, layerW, layerH, bruceConfig.priColor);
    
    // Middle Layer
    tft.drawRect(cx - layerW / 2, cy - layerH / 2, layerW, layerH, bruceConfig.priColor);
    
    // Top Layer
    tft.drawRect(cx - layerW / 2, cy - 3 * layerH / 2 - pad, layerW, layerH, bruceConfig.priColor);

    // Draw a small cursor block on top of the stack
    int s = scale * 3;
    int cursorY = cy - 3 * layerH / 2 - pad + layerH / 2 - s / 2;
    tft.fillRect(cx - s, cursorY, 2 * s, s, bruceConfig.priColor);
}
