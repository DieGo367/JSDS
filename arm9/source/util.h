#ifndef JSDS_UTIL_H
#define JSDS_UTIL_H

#include "jerry/jerryscript.h"

inline void setProperty(jerry_value_t object, const char *property, jerry_value_t value) {
	jerry_value_t propString = jerry_create_string((const jerry_char_t *) property);
	jerry_release_value(jerry_set_property(object, propString, value));
	jerry_release_value(propString);
}

inline void setMethod(jerry_value_t object, const char *property, jerry_external_handler_t function) {
	jerry_value_t func = jerry_create_external_function(function);
	setProperty(object, property, func);
	jerry_release_value(func);
}

void printValue(jerry_value_t value);
void execFile(FILE *file, bool closeFile);

const char *keyboardBuffer();
void keyboardClearBuffer();
void onKeyboardKeyPress(int key);

#endif /* JSDS_UTIL_H */