#include "logging.hpp"

#include <stdlib.h>
#include <string.h>

#include "api.hpp"
#include "helpers.hpp"
#include "io/console.hpp"
#include "jerry/jerryscript.h"



const u8 MAX_PRINT_RECURSION = 0;

int indent = 0;
void logIndentAdd() { indent++; }
void logIndentRemove() { indent = indent > 0 ? indent - 1 : 0; }
void logIndent() { for (int i = 0; i < indent; i++) putchar('\t'); }

u16 valueToColor(const char *str, u16 noneColor) {
	char *endptr = NULL;
	if (str[0] == '#') {
		u32 len = strlen(str);
		if (len == 5) { // BGR15 hex code
			u16 color = strtoul(str + 1, &endptr, 16);
			if (endptr == str + 1) return noneColor;
			else return color;
		}
		else if (len == 7) { // RGB hex code
			u32 colorCode = strtoul(str + 1, &endptr, 16);
			if (endptr == str + 1) return noneColor;
			else {
				u8 red = (colorCode >> 16 & 0xFF) * 31 / 255;
				u8 green = (colorCode >> 8 & 0xFF) * 31 / 255;
				u8 blue = (colorCode & 0xFF) * 31 / 255;
				return BIT(15) | blue << 10 | green << 5 | red;
			}
		}
	}
	// raw number input (i.e. from DS.profile.color)
	u16 color = strtoul(str, &endptr, 0);
	if (endptr != str) return color;
	// list of CSS Level 2 colors + none and transparent
	else if (strcmp(str, "none") == 0) return noneColor;
	else if (strcmp(str, "transparent") == 0) return 0x0000;
	else if (strcmp(str, "black") == 0) return 0x8000;
	else if (strcmp(str, "silver") == 0) return 0xDEF7;
	else if (strcmp(str, "gray") == 0 || strcmp(str, "grey") == 0) return 0xC210;
	else if (strcmp(str, "white") == 0) return 0xFFFF;
	else if (strcmp(str, "maroon") == 0) return 0x8010;
	else if (strcmp(str, "red") == 0) return 0x801F;
	else if (strcmp(str, "purple") == 0) return 0xC010;
	else if (strcmp(str, "fuchsia") == 0 || strcmp(str, "magenta") == 0) return 0xFC1F;
	else if (strcmp(str, "green") == 0) return 0x8200;
	else if (strcmp(str, "lime") == 0) return 0x83E0;
	else if (strcmp(str, "olive") == 0) return 0x8210;
	else if (strcmp(str, "yellow") == 0) return 0x83FF;
	else if (strcmp(str, "navy") == 0) return 0xC000;
	else if (strcmp(str, "blue") == 0) return 0xFC00;
	else if (strcmp(str, "teal") == 0) return 0xC200;
	else if (strcmp(str, "aqua") == 0 || strcmp(str, "cyan") == 0) return 0xFFE0;
	else if (strcmp(str, "orange") == 0) return 0x829F;
	else return noneColor;
}

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
			char *find = strchr(pos, '%');
			if (find == NULL) break;
			*find = '\0';
			printf(pos);
			char specifier = *(find + 1);
			if (specifier == 's') { // output next param as string
				printValue(args[i]);
				pos = find + 2;
				i++;
			}
			else if (specifier == 'd' || specifier == 'i') { // output next param as integer (parseInt)
				if (jerry_value_is_symbol(args[i])) printf("NaN");
				else {
					char *string = getAsString(args[i]);
					char *endptr = NULL;
					int64_t integer = strtoll(string, &endptr, 10);
					if (endptr == string) printf("NaN");
					else printf("%lli", integer);
					free(string);
				}
				pos = find + 2;
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
				pos = find + 2;
				i++;
			}
			else if (specifier == 'o') { // output next param with "optimally useful formatting"
				logLiteral(args[i]);
				pos = find + 2;
				i++;
			}
			else if (specifier == 'O') { // output next param as object
				if (jerry_value_is_object(args[i])) logObject(args[i]);
				else logLiteral(args[i]);
				pos = find + 2;
				i++;
			}
			else if (specifier == 'c') { // use next param as CSS rule
				char *cssString = getAsString(args[i]);
				char *semi = strchr(cssString, ';');
				char *str = cssString;
				char attribute[31] = {0};
				char value[31] = {0};
				int numSet = sscanf(str, " %30[a-zA-Z0-9] : %30[a-zA-Z0-9#] ", attribute, value);
				while (numSet == 2) { // found an attribute
					if (strcmp(attribute, "color") == 0) {
						consoleSetColor(valueToColor(value, prevColor));
					}
					else if (strcmp(attribute, "background") == 0) {
						consoleSetBackground(valueToColor(value, prevBackground));
					}
					if (semi != NULL) {
						str = semi + 1;
						semi = strchr(str, ';');
						numSet = sscanf(str, " %30[a-zA-Z0-9] : %30[a-zA-Z0-9#] ", attribute, value);
					}
					else numSet = 0;
				}
				free(cssString);
				pos = find + 2;
				i++;
			}
			else {
				putchar('%');
				pos = find + 1;
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
				for (char* ch = string; (ch = strchr(ch, '"')); ch++) {
					*ch = '\0';
					printf("%s\\\"", pos);
					*ch = '"';
					pos = ch + 1;
				}
				printf(pos);
				putchar('"');
			}
			free(string);
		} break;
		case JERRY_TYPE_SYMBOL: {
			consoleSetColor(LOGCOLOR_STRING);
			jerry_value_t description = jerry_get_symbol_descriptive_string(value);
			printString(description);
			jerry_release_value(description);
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
			jerry_value_t errorThrown = jerry_get_value_from_error(value, false);
			jerry_value_t isErrorVal = jerry_binary_operation(JERRY_BIN_OP_INSTANCEOF, errorThrown, ref_Error);
			if (jerry_get_boolean_value(isErrorVal)) {
				char *message = getStringProperty(errorThrown, "message");
				char *name = getStringProperty(errorThrown, "name");
				printf("Uncaught %s: %s", name, message);
				free(message);
				free(name);
				jerry_value_t backtrace = jerry_get_internal_property(errorThrown, ref_str_backtrace);
				u32 length = jerry_get_array_length(backtrace);
				for (u32 i = 0; i < length; i++) {
					jerry_value_t traceLine = jerry_get_property_by_index(backtrace, i);
					char *step = getString(traceLine);
					for (int j = 0; j < level; j++) putchar(' ');
					printf("\n @ %s", step);
					free(step);
					jerry_release_value(traceLine);
				}
				jerry_release_value(backtrace);
			}
			else {
				printf("Uncaught ");
				logLiteral(errorThrown);
			}
			jerry_release_value(isErrorVal);
			jerry_release_value(errorThrown);
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
					for (u32 i = 0; i < length; i++) {
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
						jerry_value_t item = jerry_get_property_by_index(value, i);
						logLiteral(item, level + 1);
						jerry_release_value(item);
						if (i < length - 1) printf(", ");
					}
					printf(" ]");
				}
			}
			else if (jerry_value_is_promise(value)) {
				jerry_value_t promiseResult = jerry_get_promise_result(value);
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
						logLiteral(promiseResult);
						break;
					default: break;
				}
				printf("}");
				jerry_release_value(promiseResult);
			}
			else logObject(value, level);
			break;
		default:
			printValue(value); // catch-all, shouldn't be reachable but should work anyway if it is
	}
	consoleSetColor(prev);
}

void logObject(jerry_value_t obj, u8 level) {
	jerry_value_t keysArray = jerry_get_object_keys(obj);
	u32 length = jerry_get_array_length(keysArray);
	if (length == 0) printf("{}");
	else if (level > MAX_PRINT_RECURSION) printf("{...}");
	else {
		printf("{ ");
		for (u32 i = 0; i < length; i++) {
			jerry_value_t key = jerry_get_property_by_index(keysArray, i);
			jerry_length_t keySize;
			char* keyStr = getString(key, &keySize);
			jerry_release_value(key);
			char capture[keySize + 1];
			if (sscanf(keyStr, "%[A-Za-z0-9]", capture) > 0 && strcmp(keyStr, capture) == 0) {
				printf(keyStr);
			}
			else {
				u16 prev = consoleSetColor(LOGCOLOR_STRING);
				if (strchr(keyStr, '"') == NULL) printf("\"%s\"", keyStr);
				else if (strchr(keyStr, '\'') == NULL) printf("'%s'", keyStr);
				else {
					putchar('"');
					char *pos = keyStr;
					for (char* ch = keyStr; (ch = strchr(ch, '"')); ch++) {
						*ch = '\0';
						printf("%s\\\"", pos);
						*ch = '"';
						pos = ch + 1;
					}
					printf(pos);
					putchar('"');
				}
				consoleSetColor(prev);
			}
			printf(": ");
			jerry_value_t item = getProperty(obj, keyStr);
			free(keyStr);
			logLiteral(item, level + 1);
			jerry_release_value(item);
			if (i < length - 1) printf(", ");
		}
		printf(" }");
	}
	jerry_release_value(keysArray);
}

static u8 tableValueWidth(jerry_value_t value) {
	jerry_type_t type = jerry_value_get_type(value);
	switch (type) {
		case JERRY_TYPE_STRING: return jerry_get_string_length(value);
		case JERRY_TYPE_NUMBER:
		case JERRY_TYPE_BIGINT: {
			jerry_value_t asString = jerry_value_to_string(value);
			u32 numLen = jerry_get_string_length(asString);
			jerry_release_value(asString);
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
	u16 prev = consoleGetColor();
	jerry_type_t type = jerry_value_get_type(value);
	switch (type) {
		case JERRY_TYPE_STRING: {
			char* str = getString(value);
			printf("%-*s", width, str);
			free(str);
		} break;
		case JERRY_TYPE_NUMBER:
		case JERRY_TYPE_BIGINT: {
			consoleSetColor(LOGCOLOR_VALUE);
			jerry_value_t asString = jerry_value_to_string(value);
			u32 numLen = jerry_get_string_length(asString);
			printString(asString);
			jerry_release_value(asString);
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
	consoleSetColor(prev);
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
	u8 idxColWidth = 1;
	u8 valueColWidth = 1;
	bool allObjs = true;
	jerry_value_t sharedKeys = jerry_create_array(0);
	u32 sharedKeyCount = 0;
	bool skipSharing = false;
	jerry_value_t pushFunc = getProperty(sharedKeys, "push");
	jerry_value_t spliceFunc = getProperty(sharedKeys, "splice");
	jerry_value_t spliceArgs[2];
	spliceArgs[1] = jerry_create_number(1);

	if (argCount >= 2 && jerry_value_is_array(args[1])) {
		u32 columnsLength = jerry_get_array_length(args[1]);
		bool allStrs = true;
		for (u32 i = 0; allStrs && i < columnsLength; i++) {
			jerry_value_t value = jerry_get_property_by_index(args[1], i);
			if (jerry_value_is_string(value)) {
				jerry_release_value(jerry_call_function(pushFunc, sharedKeys, &value, 1));
				sharedKeyCount++;
			}
			else allStrs = false;
			jerry_release_value(value);
		}
		if (allStrs) skipSharing = true;
		else {
			jerry_release_value(sharedKeys);
			sharedKeys = jerry_create_array(0);
			sharedKeyCount = 0;
		}
	}

	// analysis loop
	jerry_value_t keys = jerry_get_object_keys(args[0]);
	u32 keyCount = jerry_get_array_length(keys);
	for (u32 i = 0; i < keyCount; i++) {
		jerry_value_t key = jerry_get_property_by_index(keys, i);
		u8 keyWidth = jerry_get_string_length(key);
		if (keyWidth > idxColWidth) idxColWidth = keyWidth;

		jerry_value_t value = jerry_get_property(args[0], key);
		u8 valueWidth = tableValueWidth(value);
		if (valueWidth > valueColWidth) valueColWidth = valueWidth;

		if (allObjs && jerry_value_is_object(value)) {
			if (!skipSharing) {
				jerry_value_t subKeys = jerry_get_object_keys(value);
				u32 subKeyCount = jerry_get_array_length(subKeys);
				if (i == 0) for (u32 subIdx = 0; subIdx < subKeyCount; subIdx++) {
					jerry_value_t subKey = jerry_get_property_by_index(subKeys, subIdx);
					jerry_release_value(jerry_call_function(pushFunc, sharedKeys, &subKey, 1));
					jerry_release_value(subKey);
					sharedKeyCount++;
				}
				else for (u32 sharedKeyIdx = 0; sharedKeyIdx < sharedKeyCount; sharedKeyIdx++) {
					jerry_value_t sharedKey = jerry_get_property_by_index(sharedKeys, sharedKeyIdx);
					bool found = false;
					for (u32 subIdx = 0; !found && subIdx < subKeyCount; subIdx++) {
						jerry_value_t subKey = jerry_get_property_by_index(subKeys, subIdx);
						jerry_value_t equal = jerry_binary_operation(JERRY_BIN_OP_STRICT_EQUAL, subKey, sharedKey);
						if (jerry_get_boolean_value(equal)) found = true; 
						jerry_release_value(equal);
						jerry_release_value(subKey);
					}
					if (!found) {
						spliceArgs[0] = jerry_create_number(sharedKeyIdx);
						jerry_release_value(jerry_call_function(spliceFunc, sharedKeys, spliceArgs, 2));
						jerry_release_value(spliceArgs[0]);
						sharedKeyIdx--;
						sharedKeyCount--;
					}
					jerry_release_value(sharedKey);
				}
				jerry_release_value(subKeys);
			}
		}
		else allObjs = false;
		jerry_release_value(value);
		jerry_release_value(key);
	}

	// print a 2d table
	if (allObjs) {
		// calculate column widths (only up to the minimum length will be used)
		u8 colWidths[sharedKeyCount];
		for (u32 colIdx = 0; colIdx < sharedKeyCount; colIdx++) {
			jerry_value_t colKey = jerry_get_property_by_index(sharedKeys, colIdx);
			colWidths[colIdx] = jerry_get_string_length(colKey);
			for (u32 rowIdx = 0; rowIdx < keyCount; rowIdx++) {
				jerry_value_t rowKey = jerry_get_property_by_index(keys, rowIdx);
				jerry_value_t obj = jerry_get_property(args[0], rowKey);
				jerry_value_t value = jerry_get_property(obj, colKey);
				u8 width = tableValueWidth(value);
				if (width > colWidths[colIdx]) colWidths[colIdx] = width;
				jerry_release_value(value);
				jerry_release_value(obj);
				jerry_release_value(rowKey);
			}
			jerry_release_value(colKey);
		}
		// print top row: "i" and keys
		logIndent();
		printf("%-*s", idxColWidth, "i");
		for (u32 colIdx = 0; colIdx < sharedKeyCount; colIdx++) {
			jerry_value_t key = jerry_get_property_by_index(sharedKeys, colIdx);
			char *keyStr = getString(key);
			printf("|%-*s", colWidths[colIdx], keyStr);
			free(keyStr);
			jerry_release_value(key);
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
			jerry_value_t rowKey = jerry_get_property_by_index(keys, rowIdx);
			char *keyStr = getString(rowKey);
			logIndent();
			printf("%-*s", idxColWidth, keyStr);
			free(keyStr);
			jerry_value_t obj = jerry_get_property(args[0], rowKey);
			for (u32 colIdx = 0; colIdx < sharedKeyCount; colIdx++) {
				jerry_value_t colKey = jerry_get_property_by_index(sharedKeys, colIdx);
				jerry_value_t value = jerry_get_property(obj, colKey);
				putchar('|');
				tableValuePrint(value, colWidths[colIdx]);
				jerry_release_value(value);
				jerry_release_value(colKey);
			}
			jerry_release_value(obj);
			jerry_release_value(rowKey);
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
			jerry_value_t key = jerry_get_property_by_index(keys, i);
			char *keyStr = getString(key);
			logIndent();
			printf("%-*s|", idxColWidth, keyStr);
			free(keyStr);
			// print value
			jerry_value_t value = jerry_get_property(args[0], key);
			tableValuePrint(value, valueColWidth);
			jerry_release_value(value);
			jerry_release_value(key);
			putchar('\n');
		}
	}
	jerry_release_value(keys);
	jerry_release_value(spliceArgs[1]);
	jerry_release_value(spliceFunc);
	jerry_release_value(pushFunc);
	jerry_release_value(sharedKeys);
	consoleResume();
}