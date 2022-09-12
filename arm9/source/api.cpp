#include "api.h"

#include <nds.h>
#include <stdio.h>
#include <map>
#include <string>
#include "jerry/jerryscript.h"
#include "console.h"
#include "keyboard.h"
#include "inline.h"


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
	consolePrint(args, argCount);
	return jerry_create_undefined();
}

static jerry_value_t consoleInfoHandler(CALL_INFO) {
	u16 pal = mainConsole->fontCurPal;
	mainConsole->fontCurPal = ConsolePalette::AQUA;
	consolePrint(args, argCount);
	mainConsole->fontCurPal = pal;
	return jerry_create_undefined();
}

static jerry_value_t consoleWarnHandler(CALL_INFO) {
	u16 pal = mainConsole->fontCurPal;
	mainConsole->fontCurPal = ConsolePalette::YELLOW;
	consolePrint(args, argCount);
	mainConsole->fontCurPal = pal;
	return jerry_create_undefined();
}

static jerry_value_t consoleErrorHandler(CALL_INFO) {
	u16 pal = mainConsole->fontCurPal;
	mainConsole->fontCurPal = ConsolePalette::RED;
	consolePrint(args, argCount);
	mainConsole->fontCurPal = pal;
	return jerry_create_undefined();
}

static jerry_value_t consoleAssertHandler(CALL_INFO) {
	if (argCount == 0 || !jerry_value_to_boolean(args[0])) {
		u16 pal = mainConsole->fontCurPal;
		mainConsole->fontCurPal = ConsolePalette::RED;
		printf("Assertion failed: ");
		consolePrint(args + 1, argCount - 1);
		mainConsole->fontCurPal = pal;
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleDebugHandler(CALL_INFO) {
	u16 pal = mainConsole->fontCurPal;
	mainConsole->fontCurPal = ConsolePalette::NAVY;
	consolePrint(args, argCount);
	mainConsole->fontCurPal = pal;
	return jerry_create_undefined();
}

static jerry_value_t consoleDirHandler(CALL_INFO) {
	if (argCount > 0) {
		if (jerry_value_is_object(args[0])) consolePrintObject(args[0]);
		else consolePrintLiteral(args[0]);
		putchar('\n');
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleDirxmlHandler(CALL_INFO) {
	for (u32 i = 0; i < argCount; i++) {
		consolePrintLiteral(args[i]);
		if (i < argCount - 1) putchar(' ');
	}
	putchar('\n');
	return jerry_create_undefined();
}

static jerry_value_t consoleClearHandler(CALL_INFO) {
	consoleClear();
	return jerry_create_undefined();
}

std::map<std::string, int> consoleCounter;
static jerry_value_t consoleCountHandler(CALL_INFO) {
	std::string label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		char *lbl = getString(jerry_value_to_string(args[0]), NULL, true);
		label = std::string(lbl);
		free(lbl);
	}
	else label = "default";
	if (consoleCounter.count(label) == 0) {
		printf("%s: %i\n", label.c_str(), 1);
		consoleCounter[label] = 1;
	}
	else {
		printf("%s: %i\n", label.c_str(), ++consoleCounter[label]);
	}
	return jerry_create_undefined();
}

static jerry_value_t consoleCountResetHandler(CALL_INFO) {
	std::string label;
	if (argCount > 0 && !jerry_value_is_undefined(args[0])) {
		char *lbl = getString(jerry_value_to_string(args[0]), NULL, true);
		label = std::string(lbl);
		free(lbl);
	}
	else label = "default";
	if (consoleCounter.count(label) == 0) {
		printf("Count for '%s' does not exist\n", label.c_str());
	}
	else {
		consoleCounter[label] = 0;
	}
	return jerry_create_undefined();
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
	setMethod(console, "clear", consoleClearHandler);
	setMethod(console, "count", consoleCountHandler);
	setMethod(console, "countReset", consoleCountResetHandler);
	setMethod(console, "dir", consoleDirHandler);
	setMethod(console, "dirxml", consoleDirxmlHandler);
	setMethod(console, "error", consoleErrorHandler);
	setMethod(console, "info", consoleInfoHandler);
	setMethod(console, "log", consoleLogHandler);
	setMethod(console, "warn", consoleWarnHandler);
	jerry_release_value(console);

	jerry_release_value(global);
}