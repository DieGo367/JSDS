#ifndef JSDS_INLINE_H
#define JSDS_INLINE_H

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"

// Get object property via c string. Return value must be released!
inline jerry_value_t getProperty(jerry_value_t object, const char *property) {
	jerry_value_t propString = jerry_create_string((const jerry_char_t *) property);
	jerry_value_t value = jerry_get_property(object, propString);
	jerry_release_value(propString);
	return value;
}

// Set object property via c string.
inline void setProperty(jerry_value_t object, const char *property, jerry_value_t value) {
	jerry_value_t propString = jerry_create_string((const jerry_char_t *) property);
	jerry_release_value(jerry_set_property(object, propString, value));
	jerry_release_value(propString);
}

inline jerry_value_t nameValue;
// Set object method via c string and function. "nameValue" must have been set up previously
inline void setMethod(jerry_value_t object, const char *method, jerry_external_handler_t function) {
	jerry_value_t func = jerry_create_external_function(function);
	jerry_value_t methodName = jerry_create_string((jerry_char_t *) method);

	// Function.prototype.name isn't being set automatically, so it must be defined manually
	jerry_property_descriptor_t propDesc;
	jerry_init_property_descriptor_fields(&propDesc);
	propDesc.value = methodName;
	propDesc.is_value_defined = true;
	propDesc.is_configurable = true;
	jerry_define_own_property(func, nameValue, &propDesc);

	jerry_release_value(jerry_set_property(object, methodName, func));

	jerry_release_value(methodName);
	jerry_release_value(func);
}

/*
 * Copy a string value into a new c string. Return value must be freed!
 * If stringSize is not NULL, the value it points to will be set to the size as reported by JerryScript (which doesn't count the terminator).
*/
inline char *getString(const jerry_value_t stringValue, jerry_length_t *stringSize = NULL) {
	jerry_length_t size = jerry_get_string_size(stringValue);
	if (stringSize != NULL) *stringSize = size;
	char *buffer = (char *) malloc(size + 1);
	jerry_string_to_utf8_char_buffer(stringValue, (jerry_char_t *) buffer, size);
	buffer[size] = '\0';
	return buffer;
}

/*
 * Copy any value into a new c string. Return value must be freed!
 * If stringSize is not NULL, the value it points to will be set to the size as reported by JerryScript (which doesn't count the terminator).
*/
inline char *getAsString(const jerry_value_t value, jerry_length_t *stringSize = NULL) {
	jerry_value_t stringVal = jerry_value_to_string(value);
	char *string = getString(stringVal, stringSize);
	jerry_release_value(stringVal);
	return string;
}

/*
 * Copy an object's string property into a new c string. Return value must be freed!
 * If stringSize is not NULL, the value it points to will be set to the size as reported by JerryScript (which doesn't count the terminator).
*/
inline char *getStringProperty(jerry_value_t object, const char *property, jerry_length_t *stringSize = NULL) {
	jerry_value_t stringVal = getProperty(object, property);
	char *string = getString(stringVal, stringSize);
	jerry_release_value(stringVal);
	return string;
}

// Print any value as a string.
inline void printValue(const jerry_value_t value) {
	char *string = getAsString(value);
	printf(string);
	free(string);
}

// Output a byte value in UTF-8 representation to the position pointed to by out. Returns a pointer to the position after the last value written.
inline char *writeBinByteToUTF8(u8 byte, char *out) {
	if (byte & BIT(7)) {
		*(out++) = 0b11000000 | (byte & 0b11000000) >> 6;
		*(out++) = 0b10000000 | (byte & 0b00111111);
	}
	else *(out++) = byte;
	return out;
}

#endif /* JSDS_INLINE_H */