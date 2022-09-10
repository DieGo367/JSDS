#include "api.h"

#include <nds.h>
#include <stdio.h>
#include "jerry/jerryscript.h"
#include "util.h"


#define CALL_INFO const jerry_value_t function, const jerry_value_t thisValue, const jerry_value_t args[], u32 argCount

static jerry_value_t closeHandler(CALL_INFO) {
	exit(0);
}

static jerry_value_t alertHandler(CALL_INFO) {
	consoleClear();
	printf("============= Alert ============");
	if (argCount > 0) printValue(args[0]);
	printf("===================== (A) OK ===");
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_A) break;
	}
	consoleClear();
	return jerry_create_undefined();
}

static jerry_value_t confirmHandler(CALL_INFO) {
	consoleClear();
	printf("============ Confirm ===========");
	if (argCount > 0) printValue(args[0]);
	printf("========= (A) OK  (B) Cancel ===");
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		u32 keys = keysDown();
		if (keys & KEY_A) return consoleClear(), jerry_create_boolean(true);
		else if (keys & KEY_B) return consoleClear(), jerry_create_boolean(false);
	}
}

static jerry_value_t promptHandler(CALL_INFO) {
	consoleClear();
	printf("============ Prompt ============");
	if (argCount > 0) printValue(args[0]);
	printf("========= (A) OK  (B) Cancel ===");
	consoleSetWindow(NULL, 0, 3, 32, 11);

	keyboardClearBuffer();
	keyboardShow();
	bool canceled = false;
	while (true) {
		swiWaitForVBlank();
		scanKeys();
		u32 keys = keysDown();
		if (keys & KEY_A || keyboardEnterPressed) break;
		else if (keys & KEY_B || keyboardEscapePressed) {
			canceled = true;
			break;
		}
		keyboardUpdate();
	}
	keyboardHide();

	consoleSetWindow(NULL, 0, 0, 32, 24);
	consoleClear();
	if (canceled) return jerry_create_undefined();
	else return jerry_create_string_from_utf8((jerry_char_t *) keyboardBuffer());
}

// https://html.spec.whatwg.org/multipage/webappapis.html#dom-atob-dev
static jerry_value_t atobHandler(CALL_INFO) {
	if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'atob': 1 argument required, but only 0 present.");
	jerry_char_t *errorMsg = (jerry_char_t *) "Failed to decode base64.";

	jerry_length_t dataSize;
	char *data = getString(jerry_value_to_string(args[0]), &dataSize, true);
	char *dataEnd = data + dataSize;

	// (1) strip ASCII whitespace
	jerry_length_t strippedSize = 0;
	for (char *ch = data; ch != dataEnd; ch++) {
		u8 n; // (8.1) find codepoint associated to character
		if (*ch == ' ' || *ch == '\t' || *ch == '\n' || *ch == '\r' || *ch == '\f') continue;
		else if (*ch >= 'A' && *ch <= 'Z') n = *ch - 'A';
		else if (*ch >= 'a' && *ch <= 'z') n = *ch - 'a' + 26;
		else if (*ch >= '0' && *ch <= '9') n = *ch - '0' + 52;
		else if (*ch == '+') n = 62;
		else if (*ch == '/') n = 63;
		else if (*ch == '=') n = 64; // equal sign will be handled later
		else { // (4) error on invalid character
			free(data);
			return jerry_create_error(JERRY_ERROR_COMMON, errorMsg);
		}
		*ch = n;
		strippedSize++;
	}
	dataEnd = data + strippedSize;
	*dataEnd = '\0';

	// (2) handle ending equal signs
	if (strippedSize > 0 && strippedSize % 4 == 0) {
		if (data[strippedSize - 1] == 64) data[--strippedSize] = '\0';
		if (data[strippedSize - 1] == 64) data[--strippedSize] = '\0';
		dataEnd = data + strippedSize;
	}
	// (3) fail on %4==1
	else if (strippedSize % 4 == 1) {
		free(data);
		return jerry_create_error(JERRY_ERROR_COMMON, errorMsg);
	}

	// (5-8) output, buffer, position and loop
	char *output = (char *) malloc((strippedSize + 1) * 2);
	char *out = output;
	int buffer = 0, bits = 0;
	for (char *ch = data; ch != dataEnd; ch++) {
		if (*ch == 64) { // (4 again) fail on equal sign not at end of string
			free(data);
			return jerry_create_error(JERRY_ERROR_COMMON, errorMsg);
		}
		buffer = buffer << 6 | *ch; // (8.2) append 6 bits to buffer
		bits += 6;
		if (bits == 24) { // (8.3) output 3 8-bit numbers when buffer reaches 24 bits
			out = writeBinByteToUTF8(buffer >> 16, out);
			out = writeBinByteToUTF8(buffer >> 8 & 0xFF, out);
			out = writeBinByteToUTF8(buffer & 0xFF, out);
			buffer = bits = 0;
		}
	}
	// (9) handle remaining bits in buffer
	if (bits == 12) out = writeBinByteToUTF8(buffer >> 4, out);
	else if (bits == 18) {
		out = writeBinByteToUTF8(buffer >> 10, out);
		out = writeBinByteToUTF8(buffer >> 2 & 0xFF, out);
	}
	*out = '\0';
	free(data);
	
	jerry_value_t result = jerry_create_string_from_utf8((jerry_char_t *) output);
	free(output);
	return result;
}

// https://html.spec.whatwg.org/multipage/webappapis.html#dom-btoa-dev
const char b64Chars[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static jerry_value_t btoaHandler(CALL_INFO) {
	if (argCount == 0) return jerry_create_error(JERRY_ERROR_TYPE, (jerry_char_t *) "Failed to execute 'btoa': 1 argument required, but only 0 present.");

	jerry_length_t dataSize;
	char *data = getString(jerry_value_to_string(args[0]), &dataSize, true);
	char *dataEnd = data + dataSize;

	// convert UTF8 representation to binary, and count size
	jerry_length_t binSize = 0;
	for (char *ch = data; ch != dataEnd; ch++) {
		if (*ch & BIT(7)) { // greater than U+007F
			if (*ch & 0b00111100) { // greater than U+00FF, is out of range and therefore invalid
				free(data);
				return jerry_create_error(JERRY_ERROR_COMMON, (jerry_char_t *) "Failed to execute 'btoa': The string to be encoded contains characters outside of the Latin1 range.");
			}
			*ch = (*ch << 6 & 0b11000000) | (*ch & 0b00111111);
			ch++;
			binSize += 2;
		}
		else binSize++;
	}
	dataEnd = data + binSize;

	// allocate just enough memory, then finish the conversion to ASCII
	char *output = (char *) malloc(((binSize + 2) / 3) * 4);
	char *out = output;
	int buffer = 0, bits = 0;
	for (char *ch = data; ch != dataEnd; ch++) {
		buffer = buffer << 8 | *ch;
		bits += 8;
		if (bits == 24) {
			*(out++) = b64Chars[buffer >> 18];
			*(out++) = b64Chars[buffer >> 12 & 0b00111111];
			*(out++) = b64Chars[buffer >> 6 & 0b00111111];
			*(out++) = b64Chars[buffer & 0b00111111];
			buffer = bits = 0;
		}
	}
	if (bits == 8) {
		*(out++) = b64Chars[buffer >> 2];
		*(out++) = b64Chars[buffer << 4 & 0b00110000];
		*(out++) = '=';
		*(out++) = '=';
	}
	else if (bits == 16) {
		*(out++) = b64Chars[buffer >> 10];
		*(out++) = b64Chars[buffer >> 4 & 0b00111111];
		*(out++) = b64Chars[buffer << 2 & 0b00111100];
		*(out++) = '=';
	}
	*out = '\0';
	free(data);

	jerry_value_t result = jerry_create_string((jerry_char_t *) output);
	free(output);
	return result;
}

static jerry_value_t consoleLogHandler(CALL_INFO) {
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
			else if (specifier == 'i') { // output next param as integer
				if (jerry_value_is_number(args[i]) || jerry_value_is_bigint(args[i])) {
					u64 num = jerry_value_as_integer(args[i]);
					printf("%lli", num);
				}
				else printf("NaN");
				pos = find + 2;
				i++;
			}
			else if (specifier == 'f') { // output next param as float
				if (jerry_value_is_number(args[i]) || jerry_value_is_bigint(args[i])) {
					double num = jerry_get_number_value(args[i]);
					printf("%f", num);
				}
				else printf("NaN");
				pos = find + 2;
				i++;
			}
			else if (specifier == 'O') { // output next param as object
				printLiteral(args[i]);
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
		if (i < argCount - 1) putchar(' ');
	}
	for (; i < argCount; i++) {
		if (jerry_value_is_string(args[i])) printValue(args[i]);
		else printLiteral(args[i]);
		if (i < argCount - 1) putchar(' ');
	}
	putchar('\n');
	return jerry_create_undefined();
}

static jerry_value_t consoleInfoHandler(CALL_INFO) {
	u16 pal = mainConsole->fontCurPal;
	mainConsole->fontCurPal = ConsolePalette::AQUA;
	jerry_value_t result = consoleLogHandler(function, thisValue, args, argCount);
	mainConsole->fontCurPal = pal;
	return result;
}

static jerry_value_t consoleWarnHandler(CALL_INFO) {
	u16 pal = mainConsole->fontCurPal;
	mainConsole->fontCurPal = ConsolePalette::YELLOW;
	jerry_value_t result = consoleLogHandler(function, thisValue, args, argCount);
	mainConsole->fontCurPal = pal;
	return result;
}

static jerry_value_t consoleErrorHandler(CALL_INFO) {
	u16 pal = mainConsole->fontCurPal;
	mainConsole->fontCurPal = ConsolePalette::RED;
	jerry_value_t result = consoleLogHandler(function, thisValue, args, argCount);
	mainConsole->fontCurPal = pal;
	return result;
}

static jerry_value_t consoleAssertHandler(CALL_INFO) {
	if (argCount == 0 || !jerry_value_to_boolean(args[0])) {
		u16 pal = mainConsole->fontCurPal;
		mainConsole->fontCurPal = ConsolePalette::RED;
		printf("Assertion failed: ");
		jerry_value_t result = consoleLogHandler(function, thisValue, args + 1, argCount - 1);
		mainConsole->fontCurPal = pal;
		return result;
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleDebugHandler(CALL_INFO) {
	u16 pal = mainConsole->fontCurPal;
	mainConsole->fontCurPal = ConsolePalette::NAVY;
	jerry_value_t result = consoleLogHandler(function, thisValue, args, argCount);
	mainConsole->fontCurPal = pal;
	return result;
}

void exposeAPI() {
	jerry_value_t global = jerry_get_global_object();
	setProperty(global, "self", global);

	setMethod(global, "alert", alertHandler);
	setMethod(global, "atob", atobHandler);
	setMethod(global, "btoa", btoaHandler);
	setMethod(global, "close", closeHandler);
	setMethod(global, "confirm", confirmHandler);
	setMethod(global, "prompt", promptHandler);
	
	jerry_value_t console = jerry_create_object();
	setProperty(global, "console", console);
	setMethod(console, "assert", consoleAssertHandler);
	setMethod(console, "debug", consoleDebugHandler);
	setMethod(console, "error", consoleErrorHandler);
	setMethod(console, "info", consoleInfoHandler);
	setMethod(console, "log", consoleLogHandler);
	setMethod(console, "warn", consoleWarnHandler);
	jerry_release_value(console);

	jerry_release_value(global);
}