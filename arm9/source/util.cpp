#include "util.h"

#include <nds.h>
#include "jerry/jerryscript.h"


jerry_value_t execFile(FILE *file, bool closeFile) {
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);
	u8 *script = (u8 *) malloc(size);
	fread(script, 1, size, file);
	if (closeFile) fclose(file);

	jerry_value_t parsedCode = jerry_parse(
		(const jerry_char_t *) "main", 4,
		(const jerry_char_t *) script, size,
		JERRY_PARSE_STRICT_MODE & JERRY_PARSE_MODULE
	);
	free(script);
	if (jerry_value_is_error(parsedCode)) return parsedCode;
	else {
		jerry_value_t result = jerry_run(parsedCode);
		jerry_release_value(parsedCode);
		return result;
	}
}

u8 MAX_PRINT_RECURSION = 10;
void printLiteral(jerry_value_t value, u8 level) {
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
				printLiteral(item);
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
				printLiteral(item, level + 1);
				jerry_release_value(item);
				if (i < length - 1) printf(", ");
			}
			printf(" ]");
		}
	}
	else if (jerry_value_is_object(value)) {
		printObject(value, level);
	}
	else printValue(value); // catch-all, shouldn't be reachable but should work anyway if it is
	mainConsole->fontCurPal = pal;
}

void printObject(jerry_value_t obj, u8 level) {
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
			printLiteral(item, level + 1);
			jerry_release_value(item);
			if (i < length - 1) printf(", ");
		}
		printf(" }");
	}
	jerry_release_value(keysArray);
}

PrintConsole *mainConsole;

const int keyboardBufferSize = 256;
char buf[keyboardBufferSize] = {0};
int idx = 0;
bool keyboardEnterPressed = false;
bool keyboardEscapePressed = false;
const char *keyboardBuffer() {
	return buf;
}
u8 keyboardBufferLen() {
	return idx;
}
void keyboardClearBuffer() {
	memset(buf, 0, keyboardBufferSize);
	idx = 0;
	keyboardEnterPressed = false;
	keyboardEscapePressed = false;
}
void onKeyboardKeyPress(int key) {
	Keyboard *kbd = keyboardGetDefault();
	keyboardEnterPressed = false;
	keyboardEscapePressed = false;
	if (key == DVK_FOLD) keyboardEscapePressed = true;
	else if (key == DVK_BACKSPACE && idx > 0) {
		buf[--idx] = '\0';
		consoleClear();
		printf(buf);
	}
	else if (key == DVK_ENTER && !kbd->shifted) keyboardEnterPressed = true;
	else if (idx < keyboardBufferSize - 1 && (key == DVK_TAB || key == DVK_ENTER || (key >= 32 && key <= 126))) {
		buf[idx++] = key;
		putchar(key);
	}
}