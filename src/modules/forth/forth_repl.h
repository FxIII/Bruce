#pragma once

#include "core/ConsoleWidget.h"

void forthREPL();
ConsoleWidget* getActiveConsoleWidget();
bool forth_repl_step_once(ConsoleWidget* widget);
