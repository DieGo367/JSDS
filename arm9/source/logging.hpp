#ifndef JSDS_LOGGING_HPP
#define JSDS_LOGGING_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



enum LogColors {
	LOGCOLOR_INFO = 0xFFE0,
	LOGCOLOR_WARN = 0x83FF,
	LOGCOLOR_ERROR = 0x801F,
	LOGCOLOR_DEBUG = 0xFC00,
	LOGCOLOR_VALUE = 0xB279,
	LOGCOLOR_NULL = 0xC60F,
	LOGCOLOR_UNDEFINED = 0xA108,
	LOGCOLOR_STRING = 0xBF12,
	LOGCOLOR_FUNCTION = 0xF6AC
};

void logIndentAdd();
void logIndentRemove();
void logIndent();

void log(const jerry_value_t args[], jerry_length_t argCount);
void logLiteral(jerry_value_t value, u8 level = 0);
void logObject(jerry_value_t value, u8 level = 0);
void logTable(const jerry_value_t args[], jerry_value_t argCount);

#endif /* JSDS_LOGGING_HPP */