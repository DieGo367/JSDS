#include "logging.hpp"

#include <stdlib.h>
#include <string.h>

#include "helpers.hpp"
#include "io/console.hpp"
#include "jerry/jerryscript.h"
#include "util/color.hpp"



const u8 MAX_PRINT_RECURSION = 0;

int indent = 0;
void logIndentAdd() { indent++; }
void logIndentRemove() { indent = indent > 0 ? indent - 1 : 0; }
void logIndent() { for (int i = 0; i < indent; i++) putchar('\t'); }

void log(const jerry_value_t args[], jerry_length_t argCount) {
	consolePause();
	u32 i = 0;
	if (argCount > 0 && jerry_value_is_string(args[0])) {
		i++;
		u16 prevColor = consoleGetColor();
		u16 prevBackground = consoleGetBackground();
		char *msg = getString(args[0]);
		char *pos = msg;
		if (pos) while (i < argCount) {
			char *escapedPos = strchr(pos, '%');
			if (escapedPos == NULL) break;
			*escapedPos = '\0';
			printf(pos);
			char specifier = *(escapedPos + 1);
			if (specifier == 's') { // output next param as string
				printValue(args[i]);
				pos = escapedPos + 2;
				i++;
			}
			else if (specifier == 'd' || specifier == 'i') { // output next param as integer (parseInt)
				if (jerry_value_is_symbol(args[i])) printf("NaN");
				else {
					char *string = getAsString(args[i]);
					char *endptr = NULL;
					s64 integer = strtoll(string, &endptr, 10);
					if (endptr == string) printf("NaN");
					else printf("%lli", integer);
					free(string);
				}
				pos = escapedPos + 2;
				i++;
			}
			else if (specifier == 'f') { // output next param as float (parseFloat)
				if (jerry_value_is_symbol(args[i])) printf("NaN");
				else {
					char *string = getAsString(args[i]);
					char *endptr = NULL;
					double floatVal = strtod(string, &endptr);
					if (endptr == string) printf("NaN");
					else printf("%lg", floatVal);
					free(string);
				}
				pos = escapedPos + 2;
				i++;
			}
			else if (specifier == 'o') { // output next param with "optimally useful formatting"
				logLiteral(args[i]);
				pos = escapedPos + 2;
				i++;
			}
			else if (specifier == 'O') { // output next param as object
				if (jerry_value_is_object(args[i])) logObject(args[i]);
				else logLiteral(args[i]);
				pos = escapedPos + 2;
				i++;
			}
			else if (specifier == 'c') { // use next param as CSS rule
				char *cssRule = getAsString(args[i]);
				char *semicolon = strchr(cssRule, ';');
				char *cssPos = cssRule;
				char attribute[31] = {0};
				char value[31] = {0};
				int scanOutputCount = sscanf(cssPos, " %30[a-zA-Z0-9] : %30[a-zA-Z0-9#] ", attribute, value);
				while (scanOutputCount == 2) { // found an attribute
					if (strcmp(attribute, "color") == 0) {
						consoleSetColor(colorParse(value, prevColor));
					}
					else if (strcmp(attribute, "background") == 0) {
						consoleSetBackground(colorParse(value, prevBackground));
					}
					if (semicolon != NULL) {
						cssPos = semicolon + 1;
						semicolon = strchr(cssPos, ';');
						scanOutputCount = sscanf(cssPos, " %30[a-zA-Z0-9] : %30[a-zA-Z0-9#] ", attribute, value);
					}
					else scanOutputCount = 0;
				}
				free(cssRule);
				pos = escapedPos + 2;
				i++;
			}
			else {
				putchar('%');
				pos = escapedPos + 1;
			}
		}
		printf(pos);
		free(msg);
		consoleSetColor(prevColor);
		consoleSetBackground(prevBackground);
		if (i < argCount) putchar(' ');
	}
	for (; i < argCount; i++) {
		if (jerry_value_is_string(args[i])) printString(args[i]);
		else logLiteral(args[i]);
		if (i < argCount - 1) putchar(' ');
	}
	putchar('\n');
	consoleResume();
}

void logLiteral(jerry_value_t value, u8 level) {
	u16 prev = consoleGetColor();
	jerry_type_t type = jerry_value_get_type(value);
	switch (type) {
		case JERRY_TYPE_BOOLEAN:
		case JERRY_TYPE_NUMBER:
		case JERRY_TYPE_BIGINT: {}
			consoleSetColor(LOGCOLOR_VALUE);
			printValue(value);
			if (type == JERRY_TYPE_BIGINT) putchar('n');
			break;
		case JERRY_TYPE_NULL:
			consoleSetColor(LOGCOLOR_NULL);
			printf("null");
			break;
		case JERRY_TYPE_UNDEFINED:
			consoleSetColor(LOGCOLOR_UNDEFINED);
			printf("undefined");
			break;
		case JERRY_TYPE_STRING: {
			consoleSetColor(LOGCOLOR_STRING);
			char *string = getString(value);
			if (strchr(string, '"') == NULL) printf("\"%s\"", string);
			else if (strchr(string, '\'') == NULL) printf("'%s'", string);
			else if (strchr(string, '`') == NULL) printf("`%s`", string);
			else {
				putchar('"');
				char *pos = string;
				for (char* quote = string; (quote = strchr(quote, '"')); quote++) {
					*quote = '\0';
					printf("%s\\\"", pos);
					*quote = '"';
					pos = quote + 1;
				}
				printf(pos);
				putchar('"');
			}
			free(string);
		} break;
		case JERRY_TYPE_SYMBOL: {
			consoleSetColor(LOGCOLOR_STRING);
			jerry_value_t descriptionStr = jerry_get_symbol_descriptive_string(value);
			printString(descriptionStr);
			jerry_release_value(descriptionStr);
		} break;
		case JERRY_TYPE_FUNCTION: {
			consoleSetColor(LOGCOLOR_FUNCTION);
			char *name = getStringProperty(value, "name");
			putchar('[');
			if (jerry_value_is_async_function(value)) printf("Async");
			printf("Function");
			if (*name) printf(": %s]", name);
			else putchar(']');
			free(name);
		} break;
		case JERRY_TYPE_ERROR: {
			jerry_value_t thrownVal = jerry_get_value_from_error(value, false);
			if (isInstance(thrownVal, ref_Error)) {
				char *message = getStringProperty(thrownVal, "message");
				char *name = getStringProperty(thrownVal, "name");
				printf("Uncaught %s: %s", name, message);
				free(message);
				free(name);
				jerry_value_t backtraceArr = jerry_get_internal_property(thrownVal, ref_str_backtrace);
				u32 length = jerry_get_array_length(backtraceArr);
				for (u32 i = 0; i < length; i++) {
					jerry_value_t traceLineStr = jerry_get_property_by_index(backtraceArr, i);
					char *traceLine = getString(traceLineStr);
					for (int j = 0; j < level; j++) putchar(' ');
					printf("\n @ %s", traceLine);
					free(traceLine);
					jerry_release_value(traceLineStr);
				}
				jerry_release_value(backtraceArr);
			}
			else {
				printf("Uncaught ");
				logLiteral(thrownVal);
			}
			jerry_release_value(thrownVal);
		} break;
		case JERRY_TYPE_OBJECT:
			if (jerry_value_is_typedarray(value)) {
				jerry_length_t length = jerry_get_typedarray_length(value);
				jerry_typedarray_type_t type = jerry_get_typedarray_type(value);
				switch (type) {
					case JERRY_TYPEDARRAY_UINT8:		printf("Uint8");		break;
					case JERRY_TYPEDARRAY_UINT8CLAMPED:	printf("Uint8Clamped");	break;
					case JERRY_TYPEDARRAY_INT8:			printf("Int8");			break;
					case JERRY_TYPEDARRAY_UINT16:		printf("Uint16");		break;
					case JERRY_TYPEDARRAY_INT16:		printf("Int16");		break;
					case JERRY_TYPEDARRAY_UINT32:		printf("Uint32");		break;
					case JERRY_TYPEDARRAY_INT32:		printf("Int32");		break;
					case JERRY_TYPEDARRAY_FLOAT32:		printf("Float32");		break;
					case JERRY_TYPEDARRAY_FLOAT64:		printf("Float64");		break;
					case JERRY_TYPEDARRAY_BIGINT64:		printf("BigInt64");		break;
					case JERRY_TYPEDARRAY_BIGUINT64:	printf("BigUint64");	break;
					default:							printf("Typed");		break;
				}
				printf("Array(%lu) ", length);
				if (length == 0) printf("[]");
				else {
					printf("[ ");
					for (jerry_length_t i = 0; i < length; i++) {
						jerry_value_t item = jerry_get_property_by_index(value, i);
						logLiteral(item);
						jerry_release_value(item);
						if (i < length - 1) printf(", ");
					}
					printf(" ]");
				}
			}
			else if (jerry_value_is_array(value)) {
				u32 length = jerry_get_array_length(value);
				if (length == 0) printf("[]");
				else if (level > MAX_PRINT_RECURSION) printf("[...]");
				else {
					printf("[ ");
					for (u32 i = 0; i < length; i++) {
						jerry_value_t subVal = jerry_get_property_by_index(value, i);
						logLiteral(subVal, level + 1);
						jerry_release_value(subVal);
						if (i < length - 1) printf(", ");
					}
					printf(" ]");
				}
			}
			else if (jerry_value_is_promise(value)) {
				jerry_value_t promiseResultVal = jerry_get_promise_result(value);
				printf("Promise {");
				switch (jerry_get_promise_state(value)) {
					case JERRY_PROMISE_STATE_PENDING:
						consoleSetColor(LOGCOLOR_INFO);
						printf("<pending>");
						consoleSetColor(prev);
						break;
					case JERRY_PROMISE_STATE_REJECTED:
						consoleSetColor(LOGCOLOR_ERROR);
						printf("<rejected> ");
						consoleSetColor(prev);
						// intentional fall-through
					case JERRY_PROMISE_STATE_FULFILLED:
						logLiteral(promiseResultVal);
						break;
					default: break;
				}
				printf("}");
				jerry_release_value(promiseResultVal);
			}
			else logObject(value, level);
			break;
		default:
			printValue(value); // catch-all, shouldn't be reachable but should work anyway if it is
	}
	consoleSetColor(prev);
}

void logObject(jerry_value_t obj, u8 level) {
	jerry_value_t keysArr = jerry_get_object_keys(obj);
	u32 length = jerry_get_array_length(keysArr);
	if (length == 0) printf("{}");
	else if (level > MAX_PRINT_RECURSION) printf("{...}");
	else {
		printf("{ ");
		for (u32 i = 0; i < length; i++) {
			jerry_value_t keyStr = jerry_get_property_by_index(keysArr, i);
			jerry_size_t keySize;
			char* key = getString(keyStr, &keySize);
			jerry_release_value(keyStr);
			char capture[keySize + 1];
			if (sscanf(key, "%[A-Za-z0-9]", capture) > 0 && strcmp(key, capture) == 0) {
				printf(key);
			}
			else {
				u16 previousColor = consoleSetColor(LOGCOLOR_STRING);
				if (strchr(key, '"') == NULL) printf("\"%s\"", key);
				else if (strchr(key, '\'') == NULL) printf("'%s'", key);
				else {
					putchar('"');
					char *pos = key;
					for (char* quote = key; (quote = strchr(quote, '"')); quote++) {
						*quote = '\0';
						printf("%s\\\"", pos);
						*quote = '"';
						pos = quote + 1;
					}
					printf(pos);
					putchar('"');
				}
				consoleSetColor(previousColor);
			}
			printf(": ");
			jerry_value_t value = getProperty(obj, key);
			free(key);
			logLiteral(value, level + 1);
			jerry_release_value(value);
			if (i < length - 1) printf(", ");
		}
		printf(" }");
	}
	jerry_release_value(keysArr);
}

static jerry_length_t tableValueWidth(jerry_value_t value) {
	jerry_type_t type = jerry_value_get_type(value);
	switch (type) {
		case JERRY_TYPE_STRING: return jerry_get_string_length(value);
		case JERRY_TYPE_NUMBER:
		case JERRY_TYPE_BIGINT: {
			jerry_value_t valueStr = jerry_value_to_string(value);
			jerry_length_t numLen = jerry_get_string_length(valueStr);
			jerry_release_value(valueStr);
			if (type == JERRY_TYPE_BIGINT) numLen++;
			return numLen;
		}
		case JERRY_TYPE_BOOLEAN: return jerry_value_to_boolean(value) ? 4 : 5;
		case JERRY_TYPE_NULL: return 4;
		case JERRY_TYPE_FUNCTION: return 8; // Function
		case JERRY_TYPE_SYMBOL: return 6;
		case JERRY_TYPE_OBJECT:
			return 5; // [...] for arrays or {...} for other objects
		default: return 0; // undefined and anything else
	}
}

static void tableValuePrint(jerry_value_t value, u8 width) {
	u16 previousColor = consoleGetColor();
	jerry_type_t type = jerry_value_get_type(value);
	switch (type) {
		case JERRY_TYPE_STRING: {
			char* string = getString(value);
			printf("%-*s", width, string);
			free(string);
		} break;
		case JERRY_TYPE_NUMBER:
		case JERRY_TYPE_BIGINT: {
			consoleSetColor(LOGCOLOR_VALUE);
			jerry_value_t valueStr = jerry_value_to_string(value);
			u32 numLen = jerry_get_string_length(valueStr);
			printString(valueStr);
			jerry_release_value(valueStr);
			if (type == JERRY_TYPE_BIGINT) {
				numLen++;
				putchar('n');
			}
			printf("%-*s", (int) (width - numLen), "");
		} break;
		case JERRY_TYPE_BOOLEAN:
			consoleSetColor(LOGCOLOR_VALUE);
			printf("%-*s", width, jerry_value_to_boolean(value) ? "true" : "false");
			break;
		case JERRY_TYPE_NULL:
			consoleSetColor(LOGCOLOR_NULL);
			printf("%-*s", width, "null");
			break;
		case JERRY_TYPE_FUNCTION:
			consoleSetColor(LOGCOLOR_FUNCTION);
			printf("%-*s", width, "Function");
			break;
		case JERRY_TYPE_SYMBOL:
			consoleSetColor(LOGCOLOR_STRING);
			printf("%-*s", width, "Symbol");
			break;
		case JERRY_TYPE_OBJECT:
			consoleSetColor(LOGCOLOR_NULL);
			printf("%-*s", width, jerry_value_is_array(value) ? "[...]" : "{...}");
			break;
		default: printf("%-*s", width, "");; // undefined and anything else
	}
	consoleSetColor(previousColor);
}

void logTable(const jerry_value_t args[], jerry_value_t argCount) {
	consolePause();
	if (!jerry_value_is_object(args[0])) {
		logIndent();
		logLiteral(args[0]);
		putchar('\n');
		consoleResume();
		return;
	}
	u16 idxColWidth = 1;
	u16 valueColWidth = 1;
	bool allAreObjects = true;
	jerry_value_t sharedKeysArr = jerry_create_array(0);
	u32 sharedKeyCount = 0;
	bool skipSharing = false;

	if (argCount >= 2 && jerry_value_is_array(args[1])) {
		u32 columnsLength = jerry_get_array_length(args[1]);
		bool allAreStrings = true;
		for (u32 i = 0; allAreStrings && i < columnsLength; i++) {
			jerry_value_t value = jerry_get_property_by_index(args[1], i);
			if (jerry_value_is_string(value)) {
				jerry_release_value(jerry_call_function(ref_func_push, sharedKeysArr, &value, 1));
				sharedKeyCount++;
			}
			else allAreStrings = false;
			jerry_release_value(value);
		}
		if (allAreStrings) skipSharing = true;
		else {
			jerry_release_value(sharedKeysArr);
			sharedKeysArr = jerry_create_array(0);
			sharedKeyCount = 0;
		}
	}

	// analysis loop
	jerry_value_t keysArr = jerry_get_object_keys(args[0]);
	u32 keyCount = jerry_get_array_length(keysArr);
	for (u32 i = 0; i < keyCount; i++) {
		jerry_value_t key = jerry_get_property_by_index(keysArr, i);
		jerry_length_t keyWidth = jerry_get_string_length(key);
		if (keyWidth > idxColWidth) idxColWidth = keyWidth;

		jerry_value_t value = jerry_get_property(args[0], key);
		jerry_length_t valueWidth = tableValueWidth(value);
		if (valueWidth > valueColWidth) valueColWidth = valueWidth;

		if (allAreObjects && jerry_value_is_object(value)) {
			if (!skipSharing) {
				jerry_value_t subKeysArr = jerry_get_object_keys(value);
				u32 subKeyCount = jerry_get_array_length(subKeysArr);
				if (i == 0) for (u32 subIdx = 0; subIdx < subKeyCount; subIdx++) {
					jerry_value_t subKeyStr = jerry_get_property_by_index(subKeysArr, subIdx);
					jerry_release_value(jerry_call_function(ref_func_push, sharedKeysArr, &subKeyStr, 1));
					jerry_release_value(subKeyStr);
					sharedKeyCount++;
				}
				else for (u32 sharedKeyIdx = 0; sharedKeyIdx < sharedKeyCount; sharedKeyIdx++) {
					jerry_value_t sharedKeyStr = jerry_get_property_by_index(sharedKeysArr, sharedKeyIdx);
					bool found = false;
					for (u32 subIdx = 0; !found && subIdx < subKeyCount; subIdx++) {
						jerry_value_t subKeyStr = jerry_get_property_by_index(subKeysArr, subIdx);
						if (strictEqual(subKeyStr, sharedKeyStr)) found = true; 
						jerry_release_value(subKeyStr);
					}
					if (!found) {
						arraySplice(sharedKeysArr, sharedKeyIdx, 1);
						sharedKeyIdx--;
						sharedKeyCount--;
					}
					jerry_release_value(sharedKeyStr);
				}
				jerry_release_value(subKeysArr);
			}
		}
		else allAreObjects = false;
		jerry_release_value(value);
		jerry_release_value(key);
	}

	// print a 2d table
	if (allAreObjects) {
		// calculate column widths (only up to the minimum length will be used)
		u16 colWidths[sharedKeyCount];
		for (u32 colIdx = 0; colIdx < sharedKeyCount; colIdx++) {
			jerry_value_t colKeyStr = jerry_get_property_by_index(sharedKeysArr, colIdx);
			colWidths[colIdx] = jerry_get_string_length(colKeyStr);
			for (u32 rowIdx = 0; rowIdx < keyCount; rowIdx++) {
				jerry_value_t rowKeyStr = jerry_get_property_by_index(keysArr, rowIdx);
				jerry_value_t rowObject = jerry_get_property(args[0], rowKeyStr);
				jerry_value_t value = jerry_get_property(rowObject, colKeyStr);
				jerry_length_t width = tableValueWidth(value);
				if (width > colWidths[colIdx]) colWidths[colIdx] = width;
				jerry_release_value(value);
				jerry_release_value(rowObject);
				jerry_release_value(rowKeyStr);
			}
			jerry_release_value(colKeyStr);
		}
		// print top row: "i" and keys
		logIndent();
		printf("%-*s", idxColWidth, "i");
		for (u32 colIdx = 0; colIdx < sharedKeyCount; colIdx++) {
			jerry_value_t sharedKeyStr = jerry_get_property_by_index(sharedKeysArr, colIdx);
			char *sharedKey = getString(sharedKeyStr);
			printf("|%-*s", colWidths[colIdx], sharedKey);
			free(sharedKey);
			jerry_release_value(sharedKeyStr);
		}
		putchar('\n');
		// print separator line
		logIndent();
		for (u8 i = 0; i < idxColWidth; i++) putchar('-');
		for (u32 colIdx = 0; colIdx < sharedKeyCount; colIdx++) {
			putchar('+');
			for (u8 i = 0; i < colWidths[colIdx]; i++) putchar('-');
		}
		putchar('\n');
		// print for each row in the object
		for (u32 rowIdx = 0; rowIdx < keyCount; rowIdx++) {
			jerry_value_t rowKeyStr = jerry_get_property_by_index(keysArr, rowIdx);
			char *row = getString(rowKeyStr);
			logIndent();
			printf("%-*s", idxColWidth, row);
			free(row);
			jerry_value_t obj = jerry_get_property(args[0], rowKeyStr);
			for (u32 colIdx = 0; colIdx < sharedKeyCount; colIdx++) {
				jerry_value_t colKey = jerry_get_property_by_index(sharedKeysArr, colIdx);
				jerry_value_t value = jerry_get_property(obj, colKey);
				putchar('|');
				tableValuePrint(value, colWidths[colIdx]);
				jerry_release_value(value);
				jerry_release_value(colKey);
			}
			jerry_release_value(obj);
			jerry_release_value(rowKeyStr);
			putchar('\n');
		}
	}
	// print a key/value table
	else {
		// print top row: "i" and "Value"
		logIndent();
		printf("%-*s|%-*.*s\n", idxColWidth, "i", valueColWidth, valueColWidth, "Value");
		// print separator line
		logIndent();
		for (u8 i = 0; i < idxColWidth; i++) putchar('-');
		putchar('+');
		for (u8 i = 0; i < valueColWidth; i++) putchar('-');
		putchar('\n');
		// print for each value in the object
		for (u32 i = 0; i < keyCount; i++) {
			// print key
			jerry_value_t keyStr = jerry_get_property_by_index(keysArr, i);
			char *key = getString(keyStr);
			logIndent();
			printf("%-*s|", idxColWidth, key);
			free(key);
			// print value
			jerry_value_t value = jerry_get_property(args[0], keyStr);
			tableValuePrint(value, valueColWidth);
			jerry_release_value(value);
			jerry_release_value(keyStr);
			putchar('\n');
		}
	}
	jerry_release_value(keysArr);
	jerry_release_value(sharedKeysArr);
	consoleResume();
}