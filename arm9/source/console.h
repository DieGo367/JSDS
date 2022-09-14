#ifndef JSDS_CONSOLE_H
#define JSDS_CONSOLE_H

#include <nds/ndstypes.h>
#include <nds/arm9/console.h>
#include "jerry/jerryscript.h"



extern PrintConsole *mainConsole;

enum ConsolePalette {
	BLACK = 0,
	MAROON = 1 << 12,
	GREEN = 2 << 12,
	OLIVE = 3 << 12,
	NAVY = 4 << 12,
	PURPLE = 5 << 12,
	TEAL = 6 << 12,
	SILVER = 7 << 12,
	GRAY = 8 << 12,
	RED = 9 << 12,
	LIME = 10 << 12,
	YELLOW = 11 << 12,
	BLUE = 12 << 12,
	FUCHSIA = 13 << 12,
	AQUA = 14 << 12,
	WHITE = 15 << 12,
};

void consolePrint(const jerry_value_t args[], jerry_value_t argCount);
void consolePrintLiteral(jerry_value_t value, u8 level = 0);
void consolePrintObject(jerry_value_t value, u8 level = 0);
void consolePrintTable(const jerry_value_t args[], jerry_value_t argCount, int indent);

#endif /* JSDS_CONSOLE_H */