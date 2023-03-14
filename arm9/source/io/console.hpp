#ifndef JSDS_CONSOLE_HPP
#define JSDS_CONSOLE_HPP

#include <nds/ndstypes.h>



void consoleInit();

u16 consoleSetColor(u16);
u16 consoleGetColor();
u16 consoleSetBackground(u16);
u16 consoleGetBackground();
const u16 *consolePalette();

void consolePause();
void consoleResume();
void consoleClear();
void consoleShrink();
void consoleExpand();

#endif /* JSDS_CONSOLE_HPP */