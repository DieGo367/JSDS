#include "helpers.hpp"

#include <stdlib.h>

#include "api.hpp"



jerry_value_t throwError(const char *message) {
	return jerry_create_error(JERRY_ERROR_COMMON, (jerry_char_t *) message);
}
jerry_value_t throwTypeError(const char *message) {
	return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) message);
}

jerry_value_t createString(const char *str) {
	return jerry_create_string((const jerry_char_t *) str);
}
jerry_value_t createStringUTF16(const char16_t* codepoints, u32 length) {
	u8 utf8[length * 3]; // each codepoint can produce up to 3 bytes (surrogate pairs end up as 4 bytes, but that's still 2 bytes each)
	u8 *out = utf8;
	for (u32 i = 0; i < length; i++) {
		char16_t codepoint = codepoints[i];
		if (codepoint < 0x80) *(out++) = codepoint;
		else if (codepoint < 0x800) {
			out[0] = 0b11000000 | (codepoint >> 6 & 0b00011111);
			out[1] = BIT(7) | (codepoint & 0b00111111);
			out += 2;
		}
		else if (codepoint >= 0xD800 && codepoint < 0xDC00 && i < length && codepoints[i + 1] >= 0xDC00 && codepoints[i + 1] < 0xF000) {
			char16_t surrogate = codepoints[++i];
			out[0] = 0xF0 | (codepoint >> 7 & 0b111);
			out[1] = BIT(7) | (codepoint >> 1 & 0b00111111);
			out[2] = BIT(7) | (codepoint & 1) << 5 | (surrogate >> 5 & 0b00011111); // different mask than the rest
			out[3] = BIT(7) | (surrogate & 0b00111111);
			out += 4;
		}
		else {
			out[0] = 0b11100000 | (codepoint >> 12 & 0xF);
			out[1] = BIT(7) | (codepoint >> 6 & 0b00111111);
			out[2] = BIT(7) | (codepoint & 0b00111111);
			out += 3;
		}
	}
	if (!jerry_is_valid_utf8_string(utf8, out - utf8)) return throwTypeError("Invalid UTF-16");
	return jerry_create_string_sz_from_utf8(utf8, out - utf8);
}

char *getString(const jerry_value_t stringValue, jerry_length_t *stringSize) {
	jerry_length_t size = jerry_get_utf8_string_size(stringValue);
	if (stringSize != NULL) *stringSize = size;
	char *buffer = (char *) malloc(size + 1);
	jerry_string_to_utf8_char_buffer(stringValue, (jerry_char_t *) buffer, size);
	buffer[size] = '\0';
	return buffer;
}
char *getAsString(const jerry_value_t value, jerry_length_t *stringSize) {
	jerry_value_t stringVal = jerry_value_to_string(value);
	char *string = getString(stringVal, stringSize);
	jerry_release_value(stringVal);
	return string;
}

void printString(jerry_value_t stringValue) {
	char *string = getString(stringValue);
	printf(string);
	free(string);
}
void printValue(const jerry_value_t value) {
	char *string = getAsString(value);
	printf(string);
	free(string);
}

jerry_value_t getProperty(jerry_value_t object, const char *property) {
	jerry_value_t propString = jerry_create_string((const jerry_char_t *) property);
	jerry_value_t value = jerry_get_property(object, propString);
	jerry_release_value(propString);
	return value;
}
char *getStringProperty(jerry_value_t object, const char *property, jerry_length_t *stringSize) {
	jerry_value_t stringVal = getProperty(object, property);
	char *string = getString(stringVal, stringSize);
	jerry_release_value(stringVal);
	return string;
}
void setProperty(jerry_value_t object, const char *property, jerry_value_t value) {
	jerry_value_t propString = jerry_create_string((const jerry_char_t *) property);
	jerry_release_value(jerry_set_property(object, propString, value));
	jerry_release_value(propString);
}

jerry_property_descriptor_t nonEnumerableDesc = {
	.is_value_defined = true,
	.is_writable_defined = true,
	.is_writable = true,
	.is_enumerable_defined = true,
	.is_enumerable = false,
	.is_configurable_defined = true,
	.is_configurable = true
};
void setPropertyNonEnumerable(jerry_value_t object, const char *property, jerry_value_t value) {
	jerry_value_t propString = jerry_create_string((jerry_char_t *) property);
	nonEnumerableDesc.value = value;
	jerry_define_own_property(object, propString, &nonEnumerableDesc);
	jerry_release_value(propString);
}

jerry_property_descriptor_t nameDesc = {
	.is_value_defined = true,
	.is_configurable = true
};
void setMethod(jerry_value_t object, const char *method, jerry_external_handler_t function) {
	jerry_value_t func = jerry_create_external_function(function);
	// Function.prototype.name isn't being set automatically, so it must be defined manually
	nameDesc.value = jerry_create_string((jerry_char_t *) method);
	jerry_release_value(jerry_define_own_property(func, ref_str_name, &nameDesc));
	jerry_release_value(jerry_set_property(object, nameDesc.value, func));
	jerry_release_value(nameDesc.value);
	jerry_release_value(func);
}
jerry_value_t createMethod(jerry_value_t object, const char *method, jerry_external_handler_t function) {
	jerry_value_t func = jerry_create_external_function(function);
	// Function.prototype.name isn't being set automatically, so it must be defined manually
	nameDesc.value = jerry_create_string((jerry_char_t *) method);
	jerry_release_value(jerry_define_own_property(func, ref_str_name, &nameDesc));
	jerry_release_value(jerry_set_property(object, nameDesc.value, func));
	jerry_release_value(nameDesc.value);
	return func;
}
jerry_value_t createObject(jerry_value_t object, const char *name) {
	jerry_value_t ns = jerry_create_object();
	jerry_value_t nameStr = jerry_create_string((jerry_char_t *) name);
	jerry_release_value(jerry_set_property(object, nameStr, ns));
	jerry_release_value(jerry_set_property(ns, ref_sym_toStringTag, nameStr));
	jerry_release_value(nameStr);
	return ns;
}

JS_class createClass(jerry_value_t object, const char *name, jerry_external_handler_t constructor) {
	jerry_value_t classFunc = jerry_create_external_function(constructor);
	nameDesc.value = jerry_create_string((jerry_char_t *) name);
	jerry_release_value(jerry_define_own_property(classFunc, ref_str_name, &nameDesc));
	jerry_release_value(jerry_set_property(object, nameDesc.value, classFunc));
	jerry_value_t proto = jerry_create_object();
	jerry_release_value(jerry_set_property(classFunc, ref_str_prototype, proto));
	nonEnumerableDesc.value = classFunc;
	jerry_release_value(jerry_define_own_property(proto, ref_str_constructor, &nonEnumerableDesc));
	jerry_release_value(jerry_set_property(proto, ref_sym_toStringTag, nameDesc.value));
	jerry_release_value(nameDesc.value);
	return {.constructor = classFunc, .prototype = proto};
}
JS_class extendClass(jerry_value_t object, const char *name, jerry_external_handler_t constructor, jerry_value_t parentPrototype) {
	JS_class result = createClass(object, name, constructor);
	jerry_release_value(jerry_set_prototype(result.prototype, parentPrototype));
	return result;
}
void releaseClass(JS_class cls) {
	jerry_release_value(cls.constructor);
	jerry_release_value(cls.prototype);
}

jerry_property_descriptor_t getterDesc = {
	.is_get_defined = true,
	.is_enumerable_defined = true,
	.is_enumerable = true
};
void defGetter(jerry_value_t object, const char *property, jerry_external_handler_t getter) {
	getterDesc.getter = jerry_create_external_function(getter);
	jerry_value_t propString = jerry_create_string((jerry_char_t *) property);
	jerry_release_value(jerry_define_own_property(object, propString, &getterDesc));
	jerry_release_value(propString);
	jerry_release_value(getterDesc.getter);
}

jerry_value_t getInternalProperty(jerry_value_t object, const char *property) {
	jerry_value_t propString = jerry_create_string((const jerry_char_t *) property);
	jerry_value_t value = jerry_get_internal_property(object, propString);
	jerry_release_value(propString);
	return value;
}
void setInternalProperty(jerry_value_t object, const char *property, jerry_value_t value) {
	jerry_value_t propString = jerry_create_string((const jerry_char_t *) property);
	jerry_set_internal_property(object, propString, value);
	jerry_release_value(propString);
}

static jerry_value_t readonlyGetter(const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount) {
	jerry_value_t propertyValue = getProperty(function, "name");
	jerry_value_t got = jerry_get_internal_property(thisValue, propertyValue);
	jerry_release_value(propertyValue);
	return got;
}
void JS_setReadonly(jerry_value_t object, jerry_value_t property, jerry_value_t value) {
	jerry_set_internal_property(object, property, value);
	getterDesc.getter = jerry_create_external_function(readonlyGetter);
	nameDesc.value = property;
	jerry_release_value(jerry_define_own_property(getterDesc.getter, ref_str_name, &nameDesc));
	jerry_release_value(jerry_define_own_property(object, property, &getterDesc));
	jerry_release_value(getterDesc.getter);
}
void setReadonly(jerry_value_t object, const char *property, jerry_value_t value) {
	jerry_value_t propString = jerry_create_string((jerry_char_t *) property);
	JS_setReadonly(object, propString, value);
	jerry_release_value(propString);
}
void setReadonlyNumber(jerry_value_t object, const char *property, double value) {
	jerry_value_t n = jerry_create_number(value);
	setReadonly(object, property, n);
	jerry_release_value(n);
}
void setReadonlyString(jerry_value_t object, const char *property, const char *value) {
	jerry_value_t string = createString(value);
	setReadonly(object, property, string);
	jerry_release_value(string);
}
void setReadonlyStringUTF16(jerry_value_t object, const char *property, const char16_t *codepoints, u32 length) {
	jerry_value_t string = createStringUTF16(codepoints, length);
	setReadonly(object, property, string);
	jerry_release_value(string);
}

bool isNewTargetUndefined() {
	jerry_value_t newTarget = jerry_get_new_target();
	bool isUnd = jerry_value_is_undefined(newTarget);
	jerry_release_value(newTarget);
	return isUnd;
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
	else jerry_set_internal_property(thisValue, attrNameVal, JS_NULL);

	jerry_release_value(eventType);
	jerry_release_value(attrNameVal);
	return JS_UNDEFINED;
}

jerry_property_descriptor_t eventAttributeDesc = {
	.is_get_defined = true,
	.is_set_defined = true,
	.is_enumerable_defined = true,
	.is_enumerable = true
};
void defEventAttribute(jerry_value_t eventTarget, const char *attributeName) {
	nameDesc.value = jerry_create_string((jerry_char_t *) attributeName);
	jerry_set_internal_property(eventTarget, nameDesc.value, JS_NULL);
	eventAttributeDesc.getter = jerry_create_external_function(readonlyGetter);
	jerry_release_value(jerry_define_own_property(eventAttributeDesc.getter, ref_str_name, &nameDesc));
	eventAttributeDesc.setter = jerry_create_external_function(eventAttributeSetter);
	jerry_release_value(jerry_define_own_property(eventAttributeDesc.setter, ref_str_name, &nameDesc));
	jerry_release_value(jerry_define_own_property(eventTarget, nameDesc.value, &eventAttributeDesc));
	jerry_release_value(eventAttributeDesc.getter);
	jerry_release_value(eventAttributeDesc.setter);
	jerry_release_value(nameDesc.value);
}