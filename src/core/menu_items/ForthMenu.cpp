#include "ForthMenu.h"
#include "core/display.h"
#include "modules/forth/forth_repl.h"

void ForthMenu::optionsMenu() {
    forthREPL();
}

void ForthMenu::drawIcon(float scale) {
    clearIconArea();

    // Draw a simple ">" prompt icon
    int cx = iconCenterX;
    int cy = iconCenterY;
    int s  = (int)(scale * 18);

    // ">" arrow
    tft.fillTriangle(
        cx - s / 2, cy - s,
        cx - s / 2, cy + s,
        cx + s / 2, cy,
        bruceConfig.priColor
    );

    // Underscore cursor
    int cursorY = cy + s + (int)(scale * 6);
    tft.fillRect(cx - s, cursorY, 2 * s, (int)(scale * 3), bruceConfig.priColor);
}
