#ifndef JSDS_CONSOLE_HPP
#define JSDS_CONSOLE_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



void consoleInit();

u16 consoleSetColor(u16);
u16 consoleGetColor();
u16 consoleSetBackground(u16);
u16 consoleGetBackground();

#endif /* JSDS_CONSOLE_HPP */