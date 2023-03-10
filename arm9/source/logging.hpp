#ifndef JSDS_LOGGING_HPP
#define JSDS_LOGGING_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



enum LogColors {
	INFO = 0xFFE0,
	WARN = 0x83FF,
	ERROR = 0x801F,
	DEBUG = 0xFC00,
	VALUE = 0xB279,
	NULLED = 0xC60F,
	UNDEFINED = 0xA108,
	STRING = 0xBF12,
	FUNCTION = 0xF6AC
};

void logIndentAdd();
void logIndentRemove();
void logIndent();

void log(const jerry_value_t args[], jerry_length_t argCount);
void logLiteral(jerry_value_t value, u8 level = 0);
void logObject(jerry_value_t value, u8 level = 0);
void logTable(const jerry_value_t args[], jerry_value_t argCount);

#endif /* JSDS_LOGGING_HPP */