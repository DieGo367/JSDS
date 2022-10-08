#ifndef JSDS_INLINE_H
#define JSDS_INLINE_H

#include "jerry/jerryscript.h"

// global references that will be kept during the duration of the program

inline jerry_value_t ref_Event;
inline jerry_value_t ref_Error;
inline jerry_value_t ref_DOMException;
inline jerry_value_t ref_task_reportError;
inline jerry_value_t ref_task_abortSignalTimeout;
inline jerry_value_t ref_str_name;
inline jerry_value_t ref_str_constructor;
inline jerry_value_t ref_str_prototype;
inline jerry_value_t ref_str_backtrace;
inline jerry_value_t ref_proxyHandler_storage;

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

inline jerry_property_descriptor_t nonEnumerableDesc = {
	.is_value_defined = true,
	.is_writable_defined = true,
	.is_writable = true,
	.is_enumerable_defined = true,
	.is_enumerable = false,
	.is_configurable_defined = true,
	.is_configurable = true
};
// Sets a non-enumerable property via c string.
inline void setPropertyNonEnumerable(jerry_value_t object, const char *property, jerry_value_t value) {
	jerry_value_t propString = jerry_create_string((jerry_char_t *) property);
	nonEnumerableDesc.value = value;
	jerry_define_own_property(object, propString, &nonEnumerableDesc);
	jerry_release_value(propString);
}

inline jerry_property_descriptor_t nameDesc = {
	.is_value_defined = true,
	.is_configurable = true
};
// Set object method via c string and function.
inline void setMethod(jerry_value_t object, const char *method, jerry_external_handler_t function) {
	jerry_value_t func = jerry_create_external_function(function);
	// Function.prototype.name isn't being set automatically, so it must be defined manually
	nameDesc.value = jerry_create_string((jerry_char_t *) method);
	jerry_release_value(jerry_define_own_property(func, ref_str_name, &nameDesc));
	jerry_release_value(jerry_set_property(object, nameDesc.value, func));
	jerry_release_value(nameDesc.value);
	jerry_release_value(func);
}

struct jsClass {
	jerry_value_t constructor;
	jerry_value_t prototype;
};
/* Creates a class on object via c string and function.
 * Returns a jsClass struct containing the constructor and prototype function values.
 * Both functions must be released!
 */
inline jsClass createClass(jerry_value_t object, const char *name, jerry_external_handler_t constructor) {
	jerry_value_t classFunc = jerry_create_external_function(constructor);
	nameDesc.value = jerry_create_string((jerry_char_t *) name);
	jerry_release_value(jerry_define_own_property(classFunc, ref_str_name, &nameDesc));
	jerry_release_value(jerry_set_property(object, nameDesc.value, classFunc));
	jerry_release_value(nameDesc.value);
	jerry_value_t proto = jerry_create_object();
	jerry_release_value(jerry_set_property(classFunc, ref_str_prototype, proto));
	nonEnumerableDesc.value = classFunc;
	jerry_release_value(jerry_define_own_property(proto, ref_str_constructor, &nonEnumerableDesc));
	return {.constructor = classFunc, .prototype = proto};
}

/* Creates a class on object via c string and function, which extends an existing class via its prototype.
 * Returns a jsClass struct containing the constructor and prototype function values.
 * Both functions must be released!
 */
inline jsClass extendClass(jerry_value_t object, const char *name, jerry_external_handler_t constructor, jerry_value_t parentPrototype) {
	jsClass result = createClass(object, name, constructor);
	jerry_release_value(jerry_set_prototype(result.prototype, parentPrototype));
	return result;
}

// Releases both the constructor and prototype of a class.
inline void releaseClass(jsClass cls) {
	jerry_release_value(cls.constructor);
	jerry_release_value(cls.prototype);
}

inline jerry_property_descriptor_t getterDesc = {
	.is_get_defined = true,
	.is_enumerable_defined = true,
	.is_enumerable = true
};
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

// Create a getter on an object via jerry value that returns the value of an internal property.
inline void setReadonlyJV(jerry_value_t object, jerry_value_t property, jerry_value_t value) {
	jerry_set_internal_property(object, property, value);
	getterDesc.getter = jerry_create_external_function(internalGetter);
	nameDesc.value = property;
	jerry_release_value(jerry_define_own_property(getterDesc.getter, ref_str_name, &nameDesc));
	jerry_release_value(jerry_define_own_property(object, property, &getterDesc));
	jerry_release_value(getterDesc.getter);
}

// Create a getter on an object via c string that returns the value of an internal property.
inline void setReadonly(jerry_value_t object, const char *property, jerry_value_t value) {
	jerry_value_t propString = jerry_create_string((jerry_char_t *) property);
	setReadonlyJV(object, propString, value);
	jerry_release_value(propString);
}

// Create a getter on both the constructor and prototype of a class for a certain property via c string and function.
inline void classDefGetter(jsClass cls, const char *property, jerry_external_handler_t getter) {
	getterDesc.getter = jerry_create_external_function(getter);
	jerry_value_t propString = jerry_create_string((jerry_char_t *) property);
	jerry_release_value(jerry_define_own_property(cls.constructor, propString, &getterDesc));
	jerry_release_value(jerry_define_own_property(cls.prototype, propString, &getterDesc));
	jerry_release_value(propString);
	jerry_release_value(getterDesc.getter);
}

static jerry_value_t eventAttributeSetter(const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount) {
	jerry_value_t attrNameVal = getProperty(function, "name");
	jerry_length_t size = jerry_get_string_size(attrNameVal);
	char *attrName = (char *) malloc(size + 1);
	jerry_string_to_utf8_char_buffer(attrNameVal, (jerry_char_t *) attrName, size);
	attrName[size] = '\0';
	jerry_value_t eventType = jerry_create_string_sz((jerry_char_t *) (attrName + 2), size - 2); // skip "on" prefix
	free(attrName);

	jerry_value_t storedCallback = jerry_get_internal_property(thisValue, attrNameVal);
	if (jerry_value_is_null(storedCallback) == false) {
		jerry_value_t remove = getProperty(thisValue, "removeEventListener");
		jerry_value_t removeArgs[2] = {eventType, storedCallback};
		jerry_release_value(jerry_call_function(remove, thisValue, removeArgs, 2));
		jerry_release_value(remove);
	}
	jerry_release_value(storedCallback);

	if (jerry_value_is_function(args[0])) {
		jerry_set_internal_property(thisValue, attrNameVal, args[0]);
		jerry_value_t add = getProperty(thisValue, "addEventListener");
		jerry_value_t addArgs[2] = {eventType, args[0]};
		jerry_release_value(jerry_call_function(add, thisValue, addArgs, 2));
		jerry_release_value(add);
	}
	else {
		jerry_value_t null = jerry_create_null();
		jerry_set_internal_property(thisValue, attrNameVal, null);
		jerry_release_value(null);
	}

	jerry_release_value(eventType);
	jerry_release_value(attrNameVal);
	return jerry_create_undefined();
}

inline jerry_property_descriptor_t eventAttributeDesc = {
	.is_get_defined = true,
	.is_set_defined = true,
	.is_enumerable_defined = true,
	.is_enumerable = true
};
// Shortcut for making event handlers on event targets.
inline void defEventAttribute(jerry_value_t eventTarget, const char *attributeName) {
	nameDesc.value = jerry_create_string((jerry_char_t *) attributeName);
	jerry_value_t null = jerry_create_null();
	jerry_set_internal_property(eventTarget, nameDesc.value, null);
	jerry_release_value(null);
	eventAttributeDesc.getter = jerry_create_external_function(internalGetter);
	jerry_release_value(jerry_define_own_property(eventAttributeDesc.getter, ref_str_name, &nameDesc));
	eventAttributeDesc.setter = jerry_create_external_function(eventAttributeSetter);
	jerry_release_value(jerry_define_own_property(eventAttributeDesc.setter, ref_str_name, &nameDesc));
	jerry_release_value(jerry_define_own_property(eventTarget, nameDesc.value, &eventAttributeDesc));
	jerry_release_value(eventAttributeDesc.getter);
	jerry_release_value(eventAttributeDesc.setter);
	jerry_release_value(nameDesc.value);
}

// Creates a DOMException with the given message and name.
inline jerry_value_t createDOMException(const char *message, const char *name) {
	jerry_value_t args[2] = {jerry_create_string((jerry_char_t *) message), jerry_create_string((jerry_char_t *) name)};
	jerry_value_t exception = jerry_construct_object(ref_DOMException, args, 2);
	jerry_release_value(args[0]);
	jerry_release_value(args[1]);
	jerry_value_t backtrace = jerry_get_backtrace(10);
	jerry_set_internal_property(exception, ref_str_backtrace, backtrace);
	jerry_release_value(backtrace);
	return exception;
}

// Creates a new DOMException and wraps it in a jerry error.
inline jerry_value_t throwDOMException(const char *message, const char *name) {
	return jerry_create_error_from_value(createDOMException(message, name), true);
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