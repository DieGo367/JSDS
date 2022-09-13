#include "console.h"

#include <nds.h>
#include "jerry/jerryscript.h"
#include "inline.h"



PrintConsole *mainConsole;

const u8 MAX_PRINT_RECURSION = 10;

void consolePrint(const jerry_value_t args[], jerry_length_t argCount) {
	u32 i = 0;
	if (argCount > 0 && jerry_value_is_string(args[0])) {
		i++;
		u16 pal = mainConsole->fontCurPal;
		char *msg = getString(args[0], NULL, false);
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
					char *string = getString(jerry_value_to_string(args[i]), NULL, true);
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
					char *string = getString(jerry_value_to_string(args[i]), NULL, true);
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
				char *cssString = getString(jerry_value_to_string(args[i]), NULL, true);
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
		if (jerry_value_is_string(args[i])) printValue(args[i]);
		else consolePrintLiteral(args[i]);
		if (i < argCount - 1) putchar(' ');
	}
	putchar('\n');
}

void consolePrintLiteral(jerry_value_t value, u8 level) {
	u16 pal = mainConsole->fontCurPal;
	bool test = false;
	if (jerry_value_is_boolean(value) || jerry_value_is_number(value) || (test = jerry_value_is_bigint(value))) {
		mainConsole->fontCurPal = ConsolePalette::YELLOW;
		printValue(value);
		if (test) putchar('n');
	}
	else if (jerry_value_is_null(value)) {
		mainConsole->fontCurPal = ConsolePalette::SILVER;
		printf("null");
	}
	else if (jerry_value_is_undefined(value)) {
		mainConsole->fontCurPal = ConsolePalette::GRAY;
		printf("undefined");
	}
	else if (jerry_value_is_string(value)) {
		mainConsole->fontCurPal = ConsolePalette::LIME;
		char *string = getString(value, NULL, false);
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
	}
	else if (jerry_value_is_symbol(value)) {
		mainConsole->fontCurPal = ConsolePalette::LIME;
		jerry_value_t description = jerry_get_symbol_descriptive_string(value);
		printValue(description);
		jerry_release_value(description);
	}
	else if (jerry_value_is_function(value)) {
		mainConsole->fontCurPal = ConsolePalette::AQUA;
		char* name = getString(getProperty(value, "name"), NULL, true);
		putchar('[');
		if (jerry_value_is_async_function(value)) printf("Async");
		printf("Function");
		if (*name) printf(": %s]", name);
		else putchar(']');
		free(name);
	}
	else if (jerry_value_is_typedarray(value)) {
		jerry_length_t length = jerry_get_typedarray_length(value);
		jerry_typedarray_type_t type = jerry_get_typedarray_type(value);
		switch (type) {
			case JERRY_TYPEDARRAY_UINT8:		printf("Uint8"); break;
			case JERRY_TYPEDARRAY_UINT8CLAMPED:	printf("Uint8Clamped"); break;
			case JERRY_TYPEDARRAY_INT8:			printf("Int8"); break;
			case JERRY_TYPEDARRAY_UINT16:		printf("Uint16"); break;
			case JERRY_TYPEDARRAY_INT16:		printf("Int16"); break;
			case JERRY_TYPEDARRAY_UINT32:		printf("Uint32"); break;
			case JERRY_TYPEDARRAY_INT32:		printf("Int32"); break;
			case JERRY_TYPEDARRAY_FLOAT32:		printf("Float32"); break;
			case JERRY_TYPEDARRAY_FLOAT64:		printf("Float64"); break;
			case JERRY_TYPEDARRAY_BIGINT64:		printf("BigInt64"); break;
			case JERRY_TYPEDARRAY_BIGUINT64:	printf("BigUint64"); break;
			default:							printf("Typed"); break;
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
	else if (jerry_value_is_object(value)) {
		consolePrintObject(value, level);
	}
	else printValue(value); // catch-all, shouldn't be reachable but should work anyway if it is
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
			jerry_length_t keySize;
			char* key = getString(jerry_get_property_by_index(keysArray, i), &keySize, true);
			char capture[keySize + 1];
			if (sscanf(key, "%[A-Za-z0-9]", capture) > 0 && strcmp(key, capture) == 0) {
				printf(key);
			}
			else {
				u16 pal = mainConsole->fontCurPal;
				mainConsole->fontCurPal = ConsolePalette::LIME;
				if (strchr(key, '"') == NULL) printf("\"%s\"", key);
				else if (strchr(key, '\'') == NULL) printf("'%s'", key);
				else {
					putchar('"');
					char *pos = key;
					for (char* ch = key; (ch = strchr(ch, '"')); ch++) {
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
			jerry_value_t item = getProperty(obj, key);
			free(key);
			consolePrintLiteral(item, level + 1);
			jerry_release_value(item);
			if (i < length - 1) printf(", ");
		}
		printf(" }");
	}
	jerry_release_value(keysArray);
}
