#ifndef JSDS_UTIL_H
#define JSDS_UTIL_H

#include <nds.h>
#include "jerry/jerryscript.h"

inline jerry_value_t getProperty(jerry_value_t object, const char *property) {
	jerry_value_t propString = jerry_create_string((const jerry_char_t *) property);
	jerry_value_t value = jerry_get_property(object, propString);
	jerry_release_value(propString);
	return value;
}

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

inline char *getString(jerry_value_t stringValue, bool free) {
	jerry_length_t size = jerry_get_string_size(stringValue);
	char *buffer = (char *) malloc(size + 1);
	jerry_string_to_utf8_char_buffer(stringValue, (jerry_char_t *) buffer, size);
	buffer[size] = '\0';
	if (free) jerry_release_value(stringValue);
	return buffer;
}

void printValue(jerry_value_t value);
jerry_value_t execFile(FILE *file, bool closeFile);

extern bool keyboardEnterPressed;
extern bool keyboardEscapePressed;
const char *keyboardBuffer();
void keyboardClearBuffer();
void onKeyboardKeyPress(int key);

#endif /* JSDS_UTIL_H */