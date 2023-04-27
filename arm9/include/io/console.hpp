#ifndef JSDS_CONSOLE_HPP
#define JSDS_CONSOLE_HPP

#include <nds/ndstypes.h>
#include "util/font.hpp"



void consoleInit(NitroFont font);

u16 consoleSetColor(u16);
u16 consoleGetColor();
u16 consoleSetBackground(u16);
u16 consoleGetBackground();
NitroFont consoleGetFont();

void consolePause();
void consoleResume();
void consoleClean();
void consoleShrink();
void consoleExpand();

#endif /* JSDS_CONSOLE_HPP */