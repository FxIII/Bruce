#ifndef __FORTH_MENU_H__
#define __FORTH_MENU_H__

#include <MenuItemInterface.h>

class ForthMenu : public MenuItemInterface {
public:
    ForthMenu() : MenuItemInterface("Forth") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return false; }
    const String& themePath() override {
        static const String empty = "";
        return empty;
    }
};

#endif
