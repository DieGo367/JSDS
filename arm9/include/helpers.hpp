#ifndef JSDS_HELPERS_HPP
#define JSDS_HELPERS_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"

#define FOR_BUTTONS(DO) \
	DO("A", KEY_A) DO("B", KEY_B) DO("X", KEY_X) DO("Y", KEY_Y) DO("L", KEY_L)  DO("R", KEY_R) \
	DO("Up", KEY_UP)  DO("Down", KEY_DOWN) DO("Left", KEY_LEFT)  DO("Right", KEY_RIGHT) \
	DO("START", KEY_START) DO("SELECT", KEY_SELECT)

// constant js values, these do not need to be freed and can be used without restraint
// Values copied from Jerry internals, would need to be changed if Jerry changes them in the future
enum JSConstants {
	JS_TRUE = 56,
	JS_FALSE = 40,
	JS_NULL = 88,
	JS_UNDEFINED = 72
};

// Creates a jerry common error.
jerry_value_t throwError(const char *message);
// Creates a jerry type error.
jerry_value_t throwTypeError(const char *message);

// Creates a js string out of a c string. Return value must be released!
jerry_value_t createString(const char *str);
// Creates a js string out of a list of UTF-16 string. Return value must be released! Throws a TypeError when invalid.
jerry_value_t createStringUTF16(const char16_t* codepoints, u32 length);

/*
 * Copy a string value into a new c string. Return value must be freed!
 * If stringSize is not NULL, the value it points to will be set to the size as reported by JerryScript (which doesn't count the terminator).
 */
char *getString(const jerry_value_t stringValue, jerry_length_t *stringSize = NULL);
/*
 * Convert any value into a new c string. Return value must be freed!
 * If stringSize is not NULL, the value it points to will be set to the size as reported by JerryScript (which doesn't count the terminator).
 */
char *getAsString(const jerry_value_t value, jerry_length_t *stringSize = NULL);

// Print a string value.
void printString(jerry_value_t stringValue);
// Print any value as a string.
void printValue(const jerry_value_t value);

// Get property (via c string) from object. Return value must be released!
jerry_value_t getProperty(jerry_value_t object, const char *property);
/*
 * Copy a string value from object via property (c string) into a new c string. Return value must be freed!
 * If stringSize is not NULL, the value it points to will be set to the size as reported by JerryScript (which doesn't count the terminator).
 */
char *getStringProperty(jerry_value_t object, const char *property, jerry_length_t *stringSize = NULL);
void setProperty(jerry_value_t object, const char *property, jerry_value_t value);
void setPropertyNonEnumerable(jerry_value_t object, const char *property, jerry_value_t value);

// Sets a function property.
void setMethod(jerry_value_t object, const char *method, jerry_external_handler_t function);
// Sets and returns function. Return value must be released!
jerry_value_t createMethod(jerry_value_t object, const char *method, jerry_external_handler_t function);
// Creates an empty object and sets it on object. Return value must be released!
jerry_value_t createObject(jerry_value_t object, const char *name);

struct JS_class {
	jerry_value_t constructor;
	jerry_value_t prototype;
};
/*
 * Creates a class on object using a function as its constructor.
 * Returns a struct containing the constructor and prototype values.
 * Both values must be released!
 */
JS_class createClass(jerry_value_t object, const char *name, jerry_external_handler_t constructor);
/* 
 * Creates a class on object which extends an existing class via its prototype.
 * Returns a struct containing the constructor and prototype values.
 * Both values must be released!
 */
JS_class extendClass(jerry_value_t object, const char *name, jerry_external_handler_t constructor, jerry_value_t parentPrototype);
// Releases both the constructor and prototype of a class.
void releaseClass(JS_class cls);

// Create a getter on an object with the given function.
void defGetter(jerry_value_t object, const char *property, jerry_external_handler_t getter);

// Return value must be released!
jerry_value_t getInternalProperty(jerry_value_t object, const char *property);
void setInternalProperty(jerry_value_t object, const char *property, jerry_value_t value);
// Create a getter on an object (property via js value) that returns the value of an internal property.
void JS_setReadonly(jerry_value_t object, jerry_value_t property, jerry_value_t value);
// Create a getter on an object (property via c string) that returns the value of an internal property.
void setReadonly(jerry_value_t object, const char *property, jerry_value_t value);
// Sets a getter to a number on object via c string and double value.
void setReadonlyNumber(jerry_value_t object, const char *property, double value);
// Sets a getter to a string on object via c strings.
void setReadonlyString(jerry_value_t object, const char *property, const char *value);
// Sets a getter to a string on object via c string and UTF-16 string.
void setReadonlyStringUTF16(jerry_value_t object, const char *property, const char16_t *codepoints, u32 length);

// Returns whether new.target is undefined.
bool isNewTargetUndefined();

// Define an event attribute getter/setter on an EventTarget (i.e. "onload").
void defEventAttribute(jerry_value_t eventTarget, const char *attributeName);

#endif /* JSDS_HELPERS_HPP */