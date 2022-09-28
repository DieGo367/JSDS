#ifndef JSDS_INLINE_H
#define JSDS_INLINE_H

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

// Sets a non-enumerable property via c string.
inline void setPropertyNonEnumerable(jerry_value_t object, const char *property, jerry_value_t value) {
	jerry_value_t propString = jerry_create_string((jerry_char_t *) property);
	jerry_property_descriptor_t propDesc = {
		.is_value_defined = true,
		.is_writable_defined = true,
		.is_writable = true,
		.is_enumerable_defined = true,
		.is_enumerable = false,
		.is_configurable_defined = true,
		.is_configurable = true,
		.value = value
	};
	jerry_define_own_property(object, propString, &propDesc);
	jerry_release_value(propString);
}

inline jerry_value_t nameValue;
inline jerry_property_descriptor_t nameDesc = {
	.is_value_defined = true,
	.is_configurable = true
};
// Set object method via c string and function. "nameValue" must have been set up previously
inline void setMethod(jerry_value_t object, const char *method, jerry_external_handler_t function) {
	jerry_value_t func = jerry_create_external_function(function);
	// Function.prototype.name isn't being set automatically, so it must be defined manually
	nameDesc.value = jerry_create_string((jerry_char_t *) method);
	jerry_release_value(jerry_define_own_property(func, nameValue, &nameDesc));
	jerry_release_value(jerry_set_property(object, nameDesc.value, func));
	jerry_release_value(nameDesc.value);
	jerry_release_value(func);
}

struct jsClass {
	jerry_value_t constructor;
	jerry_value_t prototype;
};
/* Creates a class on object via c string and function. "nameValue" must have been set up previously
 * Returns a jsClass struct containing the constructor and prototype function values.
 * Both functions must be released!
 */
inline jsClass createClass(jerry_value_t object, const char *name, jerry_external_handler_t constructor) {
	jerry_value_t classFunc = jerry_create_external_function(constructor);
	nameDesc.value = jerry_create_string((jerry_char_t *) name);
	jerry_release_value(jerry_define_own_property(classFunc, nameValue, &nameDesc));
	jerry_release_value(jerry_set_property(object, nameDesc.value, classFunc));
	jerry_release_value(nameDesc.value);
	jerry_value_t proto = jerry_create_object();
	setProperty(classFunc, "prototype", proto);
	return {.constructor = classFunc, .prototype = proto};
}

/* Creates a class on object via c string and function, which extends an existing class via its prototype.
 * "nameValue" must have been set up previously
 * Returns a jsClass struct containing the constructor and prototype function values.
 * Both functions must be released!
 */
inline jsClass extendClass(jerry_value_t object, const char *name, jerry_external_handler_t constructor, jerry_value_t parentPrototype) {
	jsClass result = createClass(object, name, constructor);
	jerry_release_value(jerry_set_prototype(result.prototype, parentPrototype));
	return result;
}

inline jerry_property_descriptor_t getterDesc = {.is_get_defined = true};
// Create a getter on an object for a certain property via c string and function.
inline void defGetter(jerry_value_t object, const char *property, jerry_external_handler_t getter) {
	getterDesc.getter = jerry_create_external_function(getter);
	jerry_value_t propString = jerry_create_string((jerry_char_t *) property);
	jerry_release_value(jerry_define_own_property(object, propString, &getterDesc));
	jerry_release_value(propString);
	jerry_release_value(getterDesc.getter);
}

// Get object internal property via c string. Return value must be released!
inline jerry_value_t getInternalProperty(jerry_value_t object, const char *property) {
	jerry_value_t propString = jerry_create_string((const jerry_char_t *) property);
	jerry_value_t value = jerry_get_internal_property(object, propString);
	jerry_release_value(propString);
	return value;
}

// Set object internal property via c string.
inline void setInternalProperty(jerry_value_t object, const char *property, jerry_value_t value) {
	jerry_value_t propString = jerry_create_string((const jerry_char_t *) property);
	jerry_set_internal_property(object, propString, value);
	jerry_release_value(propString);
}

static jerry_value_t internalGetter(const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount) {
	jerry_value_t propertyValue = getProperty(function, "name");
	jerry_value_t got = jerry_get_internal_property(thisValue, propertyValue);
	jerry_release_value(propertyValue);
	return got;
}

// Create a getter on an object that returns the value of an internal property with the same name. "nameValue" must have been set up previously
inline void defReadonly(jerry_value_t object, const char *property) {
	jerry_value_t propString = jerry_create_string((jerry_char_t *) property);
		getterDesc.getter = jerry_create_external_function(internalGetter);
			nameDesc.value = propString;
			jerry_release_value(jerry_define_own_property(getterDesc.getter, nameValue, &nameDesc));
			jerry_release_value(jerry_define_own_property(object, propString, &getterDesc));
		jerry_release_value(getterDesc.getter);
	jerry_release_value(propString);
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

// Print a string value.
inline void printString(jerry_value_t stringValue) {
	char *string = getString(stringValue);
	printf(string);
	free(string);
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