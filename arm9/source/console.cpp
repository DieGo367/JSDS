#include "console.h"

#include <stdlib.h>
#include <string.h>

#include "inline.h"
#include "jerry/jerryscript.h"



PrintConsole *mainConsole;

const u8 MAX_PRINT_RECURSION = 10;

void consolePrint(const jerry_value_t args[], jerry_length_t argCount) {
	u32 i = 0;
	if (argCount > 0 && jerry_value_is_string(args[0])) {
		i++;
		u16 pal = mainConsole->fontCurPal;
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
				consolePrintLiteral(args[i]);
				pos = find + 2;
				i++;
			}
			else if (specifier == 'O') { // output next param as object
				if (jerry_value_is_object(args[i])) consolePrintObject(args[i]);
				else consolePrintLiteral(args[i]);
				pos = find + 2;
				i++;
			}
			else if (specifier == 'c') { // use next param as CSS rule
				char *cssString = getAsString(args[i]);
				char attribute[31] = {0};
				char value[31] = {0};
				int numSet = sscanf(cssString, " %30[a-zA-Z0-9] : %30[a-zA-Z0-9] ", attribute, value);
				while (numSet == 2) { // found an attribute
					// so far only "color" is supported, not sure what else is feasable
					if (strcmp(attribute, "color") == 0) {
						if (strcmp(value, "none") == 0) mainConsole->fontCurPal = pal; // reset (fast)
						else if (strcmp(value, "black") == 0) mainConsole->fontCurPal = ConsolePalette::BLACK;
						else if (strcmp(value, "maroon") == 0) mainConsole->fontCurPal = ConsolePalette::MAROON;
						else if (strcmp(value, "green") == 0) mainConsole->fontCurPal = ConsolePalette::GREEN;
						else if (strcmp(value, "olive") == 0) mainConsole->fontCurPal = ConsolePalette::OLIVE;
						else if (strcmp(value, "navy") == 0) mainConsole->fontCurPal = ConsolePalette::NAVY;
						else if (strcmp(value, "purple") == 0) mainConsole->fontCurPal = ConsolePalette::PURPLE;
						else if (strcmp(value, "teal") == 0) mainConsole->fontCurPal = ConsolePalette::TEAL;
						else if (strcmp(value, "silver") == 0) mainConsole->fontCurPal = ConsolePalette::SILVER;
						else if (strcmp(value, "gray") == 0 || strcmp(value, "grey") == 0) mainConsole->fontCurPal = ConsolePalette::GRAY;
						else if (strcmp(value, "red") == 0) mainConsole->fontCurPal = ConsolePalette::RED;
						else if (strcmp(value, "lime") == 0) mainConsole->fontCurPal = ConsolePalette::LIME;
						else if (strcmp(value, "yellow") == 0) mainConsole->fontCurPal = ConsolePalette::YELLOW;
						else if (strcmp(value, "blue") == 0) mainConsole->fontCurPal = ConsolePalette::BLUE;
						else if (strcmp(value, "fuchsia") == 0 || strcmp(value, "magenta") == 0) mainConsole->fontCurPal = ConsolePalette::FUCHSIA;
						else if (strcmp(value, "aqua") == 0 || strcmp(value, "cyan") == 0) mainConsole->fontCurPal = ConsolePalette::AQUA;
						else if (strcmp(value, "white") == 0) mainConsole->fontCurPal = ConsolePalette::WHITE;
						else mainConsole->fontCurPal = pal; // reset
					}
					numSet = sscanf(cssString, "; %30[a-zA-Z0-9] : %30[a-zA-Z0-9] ", attribute, value);
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
		mainConsole->fontCurPal = pal;
		if (i < argCount) putchar(' ');
	}
	for (; i < argCount; i++) {
		if (jerry_value_is_string(args[i])) printString(args[i]);
		else consolePrintLiteral(args[i]);
		if (i < argCount - 1) putchar(' ');
	}
	putchar('\n');
}

void consolePrintLiteral(jerry_value_t value, u8 level) {
	u16 pal = mainConsole->fontCurPal;
	jerry_type_t type = jerry_value_get_type(value);
	switch(type) {
		case JERRY_TYPE_BOOLEAN:
		case JERRY_TYPE_NUMBER:
		case JERRY_TYPE_BIGINT: {}
			mainConsole->fontCurPal = ConsolePalette::YELLOW;
			printValue(value);
			if (type == JERRY_TYPE_BIGINT) putchar('n');
			break;
		case JERRY_TYPE_NULL:
			mainConsole->fontCurPal = ConsolePalette::SILVER;
			printf("null");
			break;
		case JERRY_TYPE_UNDEFINED:
			mainConsole->fontCurPal = ConsolePalette::GRAY;
			printf("undefined");
			break;
		case JERRY_TYPE_STRING: {
			mainConsole->fontCurPal = ConsolePalette::LIME;
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
			mainConsole->fontCurPal = ConsolePalette::LIME;
			jerry_value_t description = jerry_get_symbol_descriptive_string(value);
			printString(description);
			jerry_release_value(description);
		} break;
		case JERRY_TYPE_FUNCTION: {
			mainConsole->fontCurPal = ConsolePalette::AQUA;
			char *name = getStringProperty(value, "name");
			putchar('[');
			if (jerry_value_is_async_function(value)) printf("Async");
			printf("Function");
			if (*name) printf(": %s]", name);
			else putchar(']');
			free(name);
		} break;
		case JERRY_TYPE_ERROR: {
			jerry_error_t errorCode = jerry_get_error_type(value);
			jerry_value_t errorThrown = jerry_get_value_from_error(value, false);
			if (errorCode == JERRY_ERROR_NONE) {
				printf("Uncaught ");
				consolePrintLiteral(errorThrown);
			}
			else {
				char *message = getStringProperty(errorThrown, "message");
				char *name = getStringProperty(errorThrown, "name");
				printf("Uncaught %s: %s", name, message);
				free(message);
				free(name);
				jerry_value_t backtrace = getInternalProperty(errorThrown, "backtrace");
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
						consolePrintLiteral(item);
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
						consolePrintLiteral(item, level + 1);
						jerry_release_value(item);
						if (i < length - 1) printf(", ");
					}
					printf(" ]");
				}
			}
			else consolePrintObject(value, level);
			break;
		default:
			printValue(value); // catch-all, shouldn't be reachable but should work anyway if it is
	}
	mainConsole->fontCurPal = pal;
}

void consolePrintObject(jerry_value_t obj, u8 level) {
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
				u16 pal = mainConsole->fontCurPal;
				mainConsole->fontCurPal = ConsolePalette::LIME;
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
				mainConsole->fontCurPal = pal;
			}
			printf(": ");
			jerry_value_t item = getProperty(obj, keyStr);
			free(keyStr);
			consolePrintLiteral(item, level + 1);
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
	u16 pal = mainConsole->fontCurPal;
	jerry_type_t type = jerry_value_get_type(value);
	switch (type) {
		case JERRY_TYPE_STRING: {
			char* str = getString(value);
			printf("%-*s", width, str);
			free(str);
		} break;
		case JERRY_TYPE_NUMBER:
		case JERRY_TYPE_BIGINT: {
			mainConsole->fontCurPal = ConsolePalette::YELLOW;
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
			mainConsole->fontCurPal = ConsolePalette::YELLOW;
			printf("%-*s", width, jerry_value_to_boolean(value) ? "true" : "false");
			break;
		case JERRY_TYPE_NULL:
			mainConsole->fontCurPal = ConsolePalette::SILVER;
			printf("%-*s", width, "null");
			break;
		case JERRY_TYPE_FUNCTION:
			mainConsole->fontCurPal = ConsolePalette::AQUA;
			printf("%-*s", width, "Function");
			break;
		case JERRY_TYPE_SYMBOL:
			mainConsole->fontCurPal = ConsolePalette::LIME;
			printf("%-*s", width, "Symbol");
			break;
		case JERRY_TYPE_OBJECT:
			mainConsole->fontCurPal = ConsolePalette::SILVER;
			printf("%-*s", width, jerry_value_is_array(value) ? "[...]" : "{...}");
			break;
		default: printf("%-*s", width, "");; // undefined and anything else
	}
	mainConsole->fontCurPal = pal;
}

void consolePrintTable(const jerry_value_t args[], jerry_value_t argCount, int indent) {
	if (!jerry_value_is_object(args[0])) {
		printf("%*s", indent, "");
		consolePrintLiteral(args[0]);
		putchar('\n');
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
		printf("%*s%-*s", indent, "", idxColWidth, "i");
		for (u32 colIdx = 0; colIdx < sharedKeyCount; colIdx++) {
			jerry_value_t key = jerry_get_property_by_index(sharedKeys, colIdx);
			char *keyStr = getString(key);
			printf("|%-*s", colWidths[colIdx], keyStr);
			free(keyStr);
			jerry_release_value(key);
		}
		// print separator line
		printf("\n%*s", indent, "");
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
			printf("%*s%-*s", indent, "", idxColWidth, keyStr);
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
		printf("%*s%-*s|%-*.*s\n", indent, "", idxColWidth, "i", valueColWidth, valueColWidth, "Value");
		// print separator line
		printf("%*s", indent, "");
		for (u8 i = 0; i < idxColWidth; i++) putchar('-');
		putchar('+');
		for (u8 i = 0; i < valueColWidth; i++) putchar('-');
		putchar('\n');
		// print for each value in the object
		for (u32 i = 0; i < keyCount; i++) {
			// print key
			jerry_value_t key = jerry_get_property_by_index(keys, i);
			char *keyStr = getString(key);
			printf("%*s%-*s|", indent, "", idxColWidth, keyStr);
			free(keyStr);
			// print value
			jerry_value_t value = jerry_get_property(args[0], key);
			tableValuePrint(value, valueColWidth);
			jerry_release_value(value);
			jerry_release_value(key);
			putchar('\n');
		}
	}
	jerry_release_value(spliceArgs[1]);
	jerry_release_value(spliceFunc);
	jerry_release_value(pushFunc);
	jerry_release_value(sharedKeys);
}