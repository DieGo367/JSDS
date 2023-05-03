#ifndef JSDS_HELPERS_HPP
#define JSDS_HELPERS_HPP

#include <nds/ndstypes.h>
#include "jerry/jerryscript.h"



#define CALL_INFO const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount
// Defines a handler for a JS function.
#define FUNCTION(name) jerry_value_t name(CALL_INFO)

// Lambda JS function handler that runs some code before returning undefined
#define VOID(code) [](CALL_INFO) -> jerry_value_t { code; return JS_UNDEFINED; }

// Lambda JS function handler that returns the result of an expression
#define RETURN(expression) [](CALL_INFO) -> jerry_value_t { return expression; }

// Throws a JS error if not enough arguments are specified.
#define REQUIRE(n) if (argCount < n) return requireArgError(n, argCount);

// Test for any type and return a TypeError if failed.
#define EXPECT(test, type) if (!(test)) return TypeError("Expected type '" #type "'.")

// Require the function to be called as a constructor only.
#define CONSTRUCTOR(name) if (isNewTargetUndefined()) return TypeError("Constructor '" #name "' cannot be invoked without 'new'.")

// Constant JS values, these do not need to be freed and can be used without restraint.
// The values are copied from JerryScript's internals, and would need to be updated if they change in the future (aka this is jank)

#define JS_TRUE ((jerry_value_t) 56)
#define JS_FALSE ((jerry_value_t) 40)
#define JS_NULL ((jerry_value_t) 88)
#define JS_UNDEFINED ((jerry_value_t) 72)


struct JS_class {
	jerry_value_t constructor;
	jerry_value_t prototype;
};

// held references to JS objects
extern jerry_value_t ref_global;
extern JS_class ref_Error;
extern jerry_value_t ref_func_push;
extern jerry_value_t ref_func_slice;
extern jerry_value_t ref_func_splice;
extern jerry_value_t ref_str_name;
extern jerry_value_t ref_str_constructor;
extern jerry_value_t ref_str_prototype;
extern jerry_value_t ref_str_backtrace;
extern jerry_value_t ref_str_removed;
extern jerry_value_t ref_str_main;
extern jerry_value_t ref_sym_toStringTag;

// Function for classes that should not be constructed via new.
jerry_value_t IllegalConstructor(CALL_INFO);

// Creates a jerry common error. Return value must be released!
#define Error(message) jerry_create_error(JERRY_ERROR_COMMON, (jerry_char_t *) (message))
// Creates a jerry type error. Return value must be released!
#define TypeError(message) jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) (message))
// Creates a jerry range error. Return value must be released!
#define RangeError(message) jerry_create_error(JERRY_ERROR_RANGE, (jerry_char_t *) (message))

// Returns appropriate error for given arg count.
jerry_value_t requireArgError(u32 expected, u32 received);

// Creates a js string out of a c string. Return value must be released!
#define String(str) jerry_create_string_from_utf8((const jerry_char_t *) (str))
// Creates a js string out of a c string and size. Return value must be released!
#define StringSized(str, size) jerry_create_string_sz_from_utf8((const jerry_char_t *) (str), (size))
// Creates a js string out of a list of UTF-16 string. Return value must be released! Throws a TypeError when invalid.
jerry_value_t StringUTF16(const char16_t* codepoints, jerry_size_t length);

/*
 * Copy a string value into a new c string. Return value must be freed!
 * If stringSize is not NULL, the value it points to will be set to the size as reported by JerryScript (which doesn't count the terminator).
 */
char *rawString(const jerry_value_t stringValue, jerry_size_t *stringSize = NULL);
/*
 * Convert any value into a new c string. Return value must be freed!
 * If stringSize is not NULL, the value it points to will be set to the size as reported by JerryScript (which doesn't count the terminator).
 */
char *toRawString(const jerry_value_t value, jerry_size_t *stringSize = NULL);

// Print a string value.
void printString(jerry_value_t stringValue);
// Print any value as a string.
void printValue(const jerry_value_t value);

// Return value must be released!
jerry_value_t getProperty(jerry_value_t object, const char *property);
/*
 * Copy a string value from object into a new c string. Return value must be freed!
 * If stringSize is not NULL, the value it points to will be set to the size as reported by JerryScript (which doesn't count the terminator).
 */
char *getPropertyString(jerry_value_t object, const char *property, jerry_length_t *stringSize = NULL);
void setProperty(jerry_value_t object, const char *property, jerry_value_t value);
void setProperty(jerry_value_t object, const char *property, const char *value);

// Return value must be released!
jerry_value_t getInternal(jerry_value_t object, const char *property);
/*
 * Copy a string value from object internal into a new c string. Return value must be freed!
 * If stringSize is not NULL, the value it points to will be set to the size as reported by JerryScript (which doesn't count the terminator).
 */
char *getInternalString(jerry_value_t object, const char *property, jerry_length_t *stringSize = NULL);
void setInternal(jerry_value_t object, const char *property, jerry_value_t value);
void setInternal(jerry_value_t object, const char *property, double number);
void setInternal(jerry_value_t object, jerry_value_t property, const char *value);

// void setPropertyNonEnumerable(jerry_value_t object, const char *property, jerry_value_t value);

// Sets a function property.
void setMethod(jerry_value_t object, const char *method, jerry_external_handler_t function);
// Creates a named object and sets it on object. Return value must be released!
jerry_value_t createObject(jerry_value_t object, const char *name);

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

void setPrototype(jerry_value_t object, jerry_value_t prototype);

void defGetter(jerry_value_t object, const char *property, jerry_external_handler_t getter);
void defGetterSetter(jerry_value_t object, const char *property, jerry_external_handler_t getter, jerry_external_handler_t setter);

void defReadonly(jerry_value_t object, jerry_value_t property, jerry_value_t value);
void defReadonly(jerry_value_t object, const char *property, jerry_value_t value);
void defReadonly(jerry_value_t object, const char *property, double number);
void defReadonly(jerry_value_t object, const char *property, const char *value);
void defReadonly(jerry_value_t object, const char *property, const char16_t *codepoints, jerry_size_t length);

// Create a symbol from c string. Return value must be released!
jerry_value_t Symbol(const char *symbolName);
// Assigns a symbol to object via its string description.
void setSymbol(jerry_value_t object, jerry_value_t symbol);

// Define an event attribute getter/setter on an EventTarget (i.e. "onload").
void defEventAttribute(jerry_value_t eventTarget, const char *attributeName);

// Returns whether new.target is undefined.
bool isNewTargetUndefined();

void arraySplice(jerry_value_t array, u32 start, u32 deleteCount);

bool strictEqual(jerry_value_t a, jerry_value_t b);
bool isInstance(jerry_value_t object, JS_class ofClass);

bool testProperty(jerry_value_t object, jerry_value_t property);
bool testProperty(jerry_value_t object, const char *property);
bool testInternal(jerry_value_t object, jerry_value_t property);
bool testInternal(jerry_value_t object, const char *property);

#endif /* JSDS_HELPERS_HPP */