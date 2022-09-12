#ifndef JSDS_INLINE_H
#define JSDS_INLINE_H

#include <nds/ndstypes.h>
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

inline void setMethod(jerry_value_t object, const char *method, jerry_external_handler_t function) {
	jerry_value_t func = jerry_create_external_function(function);
	jerry_value_t methodName = jerry_create_string((jerry_char_t *) method);
	jerry_value_t nameString = jerry_create_string((jerry_char_t *) "name");

	// Function.prototype.name isn't being set automatically, so it must be defined manually
	jerry_property_descriptor_t propDesc;
	jerry_init_property_descriptor_fields(&propDesc);
	propDesc.value = methodName;
	propDesc.is_value_defined = true;
	propDesc.is_configurable = true;
	jerry_define_own_property(func, nameString, &propDesc);

	jerry_release_value(jerry_set_property(object, methodName, func));

	jerry_release_value(nameString);
	jerry_release_value(methodName);
	jerry_release_value(func);
}

inline char *getString(jerry_value_t stringValue, jerry_length_t *stringSize, bool free) {
	jerry_length_t size = jerry_get_string_size(stringValue);
	if (stringSize != NULL) *stringSize = size;
	char *buffer = (char *) malloc(size + 1);
	jerry_string_to_utf8_char_buffer(stringValue, (jerry_char_t *) buffer, size);
	buffer[size] = '\0';
	if (free) jerry_release_value(stringValue);
	return buffer;
}

inline void printValue(jerry_value_t value) {
	char *string = getString(jerry_value_to_string(value), NULL, true);
	printf(string);
	free(string);
}

inline char *writeBinByteToUTF8(u8 byte, char *out) {
	if (byte & BIT(7)) {
		*(out++) = 0b11000000 | (byte & 0b11000000) >> 6;
		*(out++) = 0b10000000 | (byte & 0b00111111);
	}
	else *(out++) = byte;
	return out;
}

#endif /* JSDS_INLINE_H */